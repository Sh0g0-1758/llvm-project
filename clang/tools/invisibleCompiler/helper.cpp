#include "helpers.h"
#include "clang/AST/Mangle.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Lex/Lexer.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/xxhash.h"

#include <algorithm>
#include <stdexcept>

namespace invisible {

// ─── Settings shared by the passes ─────────────────────────────────

ToolSettings& settings() {
    static ToolSettings tool_settings;
    return tool_settings;
}

llvm::raw_ostream& verbose() {
    return settings().verbose ? llvm::errs() : llvm::nulls();
}

void addPipelineFlags(std::vector<std::string>& args) {
    // clang declares the builtins itself, eg. `memcpy` taking a `__size_t`, a name of clang's, and a
    // hoisted temporary of their type would be declared with it rather than with `size_t`
    args.emplace_back("-fno-builtin");
    // Silence every warning, eg. C's -Wincompatible-pointer-types, which some inputs only compile
    // without. checkAsCompiled takes it back for the inputs that compile with warnings on.
    args.emplace_back("-Wno-everything");
}

// `-Wno-everything` also silences the warnings that are errors by default, eg. `-Winvalid-constexpr`:
// a pass that prints a `constexpr` specialization that can never be constant would be accepted,
// and the real build would then fail. So the input is checked once without it, and when it
// compiles so, so must the output of every pass.
void checkAsCompiled(std::vector<std::string>& args, const std::string& source, const std::string& filename) {
    auto lenient = std::find(args.begin(), args.end(), "-Wno-everything");
    if (lenient == args.end()) return;
    std::vector<std::string> strict(args.begin(), lenient);
    strict.insert(strict.end(), lenient + 1, args.end());
    // the member redeclarations DeclBeforeDef writes outside their class are accepted. The
    // warnings aren't printed, see ErrorPrinter; `-w` would silence `-Werror=` ones too.
    strict.push_back("-Wno-out-of-line-declaration");
    // the driver's warnings about the flags, eg. `-c` unused by a syntax check, are printed directly
    strict.push_back("-Wno-unused-command-line-argument");
    if (isCompilable(source, filename, strict)) args = std::move(strict);
}

// the one place isCompilable reports a failure, read back by the driver's error blaming
static CompileFailure last_compile_failure;

const CompileFailure& lastCompileFailure() {
    return last_compile_failure;
}

// ─── Clang in-memory pass infrastructure ───────────────────────────

std::string getRewrittenSource(clang::Rewriter& rewriter, const std::string& original) {
    auto& SM = rewriter.getSourceMgr();
    const llvm::RewriteBuffer* buf = rewriter.getRewriteBufferFor(SM.getMainFileID());
    if (buf)
        return std::string(buf->begin(), buf->end());
    return original;
}

namespace {

/// Prints the errors, and their notes, of a run checked as a compiler builds it, which
/// leaves warnings on, see checkAsCompiled. A note belongs to the diagnostic before it, so a
/// note after a warning is dropped with it. The driver parses these lines, eg. `a.cc:12:3: error:`,
/// and warnings in between would only be noise to it.
class ErrorPrinter : public clang::TextDiagnosticPrinter {
  public:
    using TextDiagnosticPrinter::TextDiagnosticPrinter;
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& info) override {
        if (level != clang::DiagnosticsEngine::Note) m_printing = level >= clang::DiagnosticsEngine::Error;
        if (m_printing) TextDiagnosticPrinter::HandleDiagnostic(level, info);
        else DiagnosticConsumer::HandleDiagnostic(level, info);
    }

  private:
    bool m_printing = true;
};

/// Renders the diagnostics of a clang run into a string instead of stderr: a pass parses the
/// source many times, and only the diagnostics of the run that fails are worth showing.
void captureDiagnostics(clang::CompilerInstance& ci, llvm::raw_ostream& out) {
    ci.getDiagnosticOpts().ShowColors = settings().color;
    out.enable_colors(settings().color);
    ci.getDiagnostics().setClient(new ErrorPrinter(out, ci.getDiagnosticOpts()), /*ShouldOwnClient=*/true);
    // clang prints the "N errors generated." summary there
    ci.setVerboseOutputStream(out);
}

/// Hands the whole TU, once parsed, to the pass's callback.
class InvisibleConsumer : public clang::ASTConsumer {
  public:
    explicit InvisibleConsumer(TUCallback callback, clang::CompilerInstance& ci) : m_callback(std::move(callback)), m_ci(ci) {}

    void HandleTranslationUnit(clang::ASTContext& ctx) override { m_callback(ctx, m_ci.getSema()); }

  private:
    TUCallback m_callback;
    clang::CompilerInstance& m_ci;
};

/// The frontend action of runInMemory: diagnostics captured, then InvisibleConsumer.
class InvisibleAction : public clang::ASTFrontendAction {
  public:
    InvisibleAction(TUCallback callback, llvm::raw_ostream& diagnostics)
        : m_callback(std::move(callback)), m_diagnostics(diagnostics) {}

    bool BeginInvocation(clang::CompilerInstance& ci) override {
        captureDiagnostics(ci, m_diagnostics);
        return true;
    }

  protected:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& ci, llvm::StringRef) override {
        return std::make_unique<InvisibleConsumer>(m_callback, ci);
    }

  private:
    TUCallback m_callback;
    llvm::raw_ostream& m_diagnostics;
};

}  // namespace

namespace {

// The name generator passKey made for the AST being visited. It refers to the AST, so it is
// dropped with it: a new AST can be allocated where the last one was, and a generator kept by the
// address of its AST would then name with the freed one.
struct PassKeyNames {
    const clang::ASTContext* context = nullptr;
    std::unique_ptr<clang::ASTNameGenerator> generator;
};

PassKeyNames& passKeyNames() {
    static PassKeyNames names;
    return names;
}

}  // namespace

std::string runInMemory(const std::string& source, const std::string& filename, std::vector<std::string>& args, TUCallback callback,
                        clang::Rewriter& rewriter) {
    std::string diagnostics;
    llvm::raw_string_ostream diagnostics_out(diagnostics);
    // the rewrites are read in the run, the rewriter's SourceManager is freed with it
    std::string rewritten = source;
    auto capture = [&](clang::ASTContext& ctx, clang::Sema& sema) {
        callback(ctx, sema);
        rewritten = getRewrittenSource(rewriter, source);
        // what refers to the AST, which is freed with the run
        passKeyNames() = {};
    };
    auto action = std::make_unique<InvisibleAction>(capture, diagnostics_out);
    if (!clang::tooling::runToolOnCodeWithArgs(std::move(action), source, args, filename))
        verbose() << "clang failed to parse " << filename << " while running a pass\n";
    verbose() << diagnostics;
    return rewritten;
}

// The check after every pass iteration. It parses the whole TU, so it costs about as much as the
// pass itself; the driver calls it once per iteration and once per retry.
bool isCompilable(const std::string& source, const std::string& filename,
                  const std::vector<std::string>& compiler_flags) {
    // the tool owns and destroys the action, so it reports into variables owned here.
    class CheckAction : public clang::SyntaxOnlyAction {
    public:
        CheckAction(llvm::raw_ostream& diagnostics, unsigned& errors) : m_diagnostics(diagnostics), m_errors(errors) {}
        bool BeginInvocation(clang::CompilerInstance& ci) override {
            captureDiagnostics(ci, m_diagnostics);
            return true;
        }
        void EndSourceFile() override {
            m_errors = getCompilerInstance().getDiagnostics().getClient()->getNumErrors();
            clang::SyntaxOnlyAction::EndSourceFile();
        }
    private:
        llvm::raw_ostream& m_diagnostics;
        unsigned& m_errors;
    };

    std::string diagnostics;
    llvm::raw_string_ostream diagnostics_out(diagnostics);
    unsigned errors = 0;
    bool ran = clang::tooling::runToolOnCodeWithArgs(std::make_unique<CheckAction>(diagnostics_out, errors), source,
                                                     compiler_flags, filename);
    if (ran && errors == 0)
        return true;
    last_compile_failure = {source, filename, std::move(diagnostics), std::max(errors, 1u)};
    return false;
}

namespace {

/// Whether the template argument @p arg can be named anywhere, see isNameableAnywhere: a type,
/// each element of a pack, and a declaration, eg. the address `&local_fn` can't be of a local.
bool isNameableArgument(const clang::TemplateArgument& arg) {
    switch (arg.getKind()) {
    case clang::TemplateArgument::Type:
        return isNameableAnywhere(arg.getAsType());
    case clang::TemplateArgument::Pack:
        return llvm::all_of(arg.pack_elements(), [](const clang::TemplateArgument& element) { return isNameableArgument(element); });
    case clang::TemplateArgument::Declaration:
        return !isInsideFunction(arg.getAsDecl());
    default:
        return true;
    }
}

}  // namespace

// Looks through what a type is built of: pointers, references, arrays, member pointers and
// function types, to the tag it names, then up the tag's scopes, eg. `Outer<Local>::Inner *`
// is unnameable because of `Local`, a template argument of a scope of the tag.
bool isNameableAnywhere(clang::QualType type) {
    type = type.getCanonicalType();
    if (type.isNull()) return true;
    if (const auto* pointer = type->getAs<clang::PointerType>()) return isNameableAnywhere(pointer->getPointeeType());
    if (const auto* reference = type->getAs<clang::ReferenceType>()) return isNameableAnywhere(reference->getPointeeType());
    if (const auto* array = llvm::dyn_cast<clang::ArrayType>(type)) return isNameableAnywhere(array->getElementType());
    if (const auto* member = type->getAs<clang::MemberPointerType>())
        return isNameableAnywhere(member->getPointeeType()) &&
               (!member->getMostRecentCXXRecordDecl() ||
                isNameableAnywhere(member->getMostRecentCXXRecordDecl()->getASTContext().getCanonicalTagType(member->getMostRecentCXXRecordDecl())));
    if (const auto* function = type->getAs<clang::FunctionProtoType>())
        return isNameableAnywhere(function->getReturnType()) &&
               llvm::all_of(function->param_types(), [](clang::QualType param) { return isNameableAnywhere(param); });
    const clang::TagDecl* tag = type->getAsTagDecl();
    if (!tag) return true;
    for (const clang::DeclContext* dc = tag; dc; dc = dc->getParent()) {
        // a local class
        if (llvm::isa<clang::FunctionDecl>(dc)) return false;
        const auto* decl = llvm::dyn_cast<clang::TagDecl>(dc);
        if (!decl) continue;
        // `struct { int x; } point;`, unless named by a typedef, `typedef struct { ... } Point;`
        if (!decl->getIdentifier() && !decl->getTypedefNameForAnonDecl()) return false;
        if (const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl); record && record->isLambda()) return false;
        if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl))
            for (const clang::TemplateArgument& arg : spec->getTemplateArgs().asArray())
                if (!isNameableArgument(arg)) return false;
    }
    return true;
}

// Walks the scopes the declaration is written in, not the ones it belongs to: a member function of a
// local class, `void f() { struct L { void g() {} }; }`, belongs to `L` and is written in `f`, and
// a friend defined in a class belongs to the namespace but is written in the class.
//
// A body is a function's, including a lambda's `operator()`, or a block's, `^(int x) { ... }` with
// -fblocks. A block's body is a BlockDecl, not a FunctionDecl, and a block can be outside any
// function, eg. in the initializer of a global `int (^g)(void) = ^{ struct L {...}; ... };`.
bool isInsideFunction(const clang::Decl* decl) {
    for (const clang::DeclContext* decl_context = decl->getLexicalDeclContext(); decl_context; decl_context = decl_context->getLexicalParent())
        if (llvm::isa<clang::FunctionDecl, clang::BlockDecl>(decl_context)) return true;
    return false;
}

// a constructor call isn't a CallExpr, eg. `T(x)` doesn't count, and neither does an operator of a
// built-in type; an overloaded operator, `a + b` of strings, is a CXXOperatorCallExpr and does
bool containsCall(const clang::Stmt* stmt) {
    return stmt && (llvm::isa<clang::CallExpr>(stmt) ||
                    llvm::any_of(stmt->children(), [](const clang::Stmt* child) { return containsCall(child); }));
}

clang::CharSourceRange exprCharRange(clang::SourceRange range, const clang::SourceManager& sm, const clang::LangOptions& lang_opts) {
    // the half of a split `>>` is a location of its own, expanded from the `>>`
    clang::SourceLocation begin = sm.getExpansionLoc(range.getBegin());
    clang::SourceLocation end = sm.getExpansionLoc(range.getEnd());
    if (*sm.getCharacterData(end) == '>') return clang::CharSourceRange::getCharRange(begin, end.getLocWithOffset(1));
    return clang::CharSourceRange::getCharRange(begin, clang::Lexer::getLocForEndOfToken(end, 0, sm, lang_opts));
}

// eg. replacing the expression `(x)` of `not(x)` with `res` would write `notres`, another name;
// with the spaces it writes `not res`
void replaceExpr(clang::Rewriter& rewriter, clang::SourceRange range, const std::string& text) {
    const clang::SourceManager& sm = rewriter.getSourceMgr();
    auto is_word = [](char c) { return clang::isAsciiIdentifierContinue(c); };
    clang::CharSourceRange chars = exprCharRange(range, sm, rewriter.getLangOpts());
    bool word_before = !text.empty() && is_word(text.front()) && is_word(*(sm.getCharacterData(chars.getBegin()) - 1));
    bool word_after = !text.empty() && chars.getEnd().isValid() && is_word(text.back()) && is_word(*sm.getCharacterData(chars.getEnd()));
    rewriter.ReplaceText(chars, (word_before ? " " : "") + text + (word_after ? " " : ""));
}

// clang's own option, so that the tool links against an unmodified clang: the type of an
// enumeration argument is otherwise only printed when clang deems it ambiguous
clang::PrintingPolicy codePolicy(const clang::ASTContext& ctx) {
    clang::PrintingPolicy policy(ctx.getLangOpts());
    policy.AlwaysIncludeTypeForTemplateArgument = true;
    return policy;
}

// The literal's type is the first of int, long and long long that its magnitude fits in; the suffix
// is only needed when that isn't @p type, eg. an `unsigned` 5 is `5U`, an `int` 5 is `5`.
std::string integerSuffix(const clang::ASTContext& ctx, const llvm::APSInt& value, clang::QualType type) {
    const auto* builtin = type.getCanonicalType()->getAs<clang::BuiltinType>();
    if (!builtin) return "";
    llvm::APSInt magnitude = value.extend(value.getBitWidth() + 1);
    if (magnitude.isNegative()) magnitude = -magnitude;
    unsigned bits = magnitude.getActiveBits();
    clang::BuiltinType::Kind written = bits < ctx.getIntWidth(ctx.IntTy)    ? clang::BuiltinType::Int
                                       : bits < ctx.getIntWidth(ctx.LongTy) ? clang::BuiltinType::Long
                                                                            : clang::BuiltinType::LongLong;
    if (builtin->getKind() == written) return "";
    switch (builtin->getKind()) {
    case clang::BuiltinType::UInt: return "U";
    case clang::BuiltinType::Long: return "L";
    case clang::BuiltinType::ULong: return "UL";
    case clang::BuiltinType::LongLong: return "LL";
    case clang::BuiltinType::ULongLong: return "ULL";
    default: return "";
    }
}

// 40 bits of the hash of the mangled name, in lowercase hexadecimal, eg. `size__mono1a2b3c4d5e`: two instantiations of one
// name, `vector<int>::size` and `vector<char>::size`, get different copies, and every TU gives
// `vector<int>::size` the same one, which `inline` lets the linker merge
std::string copyName(llvm::StringRef base, llvm::StringRef key) {
    return (base + "__mono" + llvm::utohexstr(llvm::xxh3_64bits(key) & 0xffffffffffULL, true)).str();
}

// told by the name copyName gives it: `__mono` and hexadecimal digits at its end
bool isCopy(const clang::FunctionDecl* func) {
    if (!func->getIdentifier()) return false;
    llvm::StringRef name = func->getName();
    size_t at = name.rfind("__mono");
    return at != llvm::StringRef::npos && at + 6 < name.size() && llvm::all_of(name.drop_front(at + 6), llvm::isHexDigit);
}

// global, as the visitors that consult it, through isLeftAsWritten, are deep in the passes
std::set<std::string>& passSkips() {
    static std::set<std::string> skips;
    return skips;
}

std::string passKey(const clang::FunctionDecl* func) {
    // a template can't be mangled, it is told by its name and number of parameters
    if (func->isDependentContext() || llvm::isa<clang::CXXDeductionGuideDecl>(func))
        return "template " + func->getQualifiedNameAsString() + "/" + std::to_string(func->getNumParams());
    // a name generator per AST: a pass parses the source anew every run, see PassKeyNames
    PassKeyNames& names = passKeyNames();
    if (names.context != &func->getASTContext()) {
        names.context = &func->getASTContext();
        names.generator = std::make_unique<clang::ASTNameGenerator>(func->getASTContext());
    }
    return names.generator->getName(func);
}

bool isSkippedByPass(const clang::FunctionDecl* func) {
    return !passSkips().empty() && func && passSkips().count(passKey(func));
}

bool isLeftAsWritten(const clang::Decl* decl) {
    if (const auto* func = llvm::dyn_cast_or_null<clang::FunctionDecl>(decl)) {
        if (isSkippedByPass(func)) return true;
        if (func->getFriendObjectKind() != clang::Decl::FOK_None) return true;
        // a constexpr function is left in place, as the compiler may evaluate
        // it in a constant expression, where what the passes write, eg. a hoisted temporary, may
        // not be allowed. The calls go to its copy, which the compiler never evaluates
        if (func->isConstexpr() && func->doesThisDeclarationHaveABody() && !isCopy(func)) return true;
    }
    // eg. the definition of a static data member of a class template
    if (const auto* var = llvm::dyn_cast_or_null<clang::VarDecl>(decl);
        var && (var->isTemplated() || var->getInstantiatedFromStaticDataMember()))
        return true;
    const auto* context = llvm::dyn_cast_or_null<clang::DeclContext>(decl);
    return context && context->isDependentContext();
}

// eg. `vector<int>::size` is instantiated; so is the declaration of `size` in `vector<int>` when
// `template <> size_t vector<int>::size() const { ... }` specializes it later, whose text is
// still the template's
bool isInstantiatedCopy(const clang::FunctionDecl* func) {
    if (func->isTemplateInstantiation()) return true;
    for (const clang::DeclContext* dc = func->getLexicalDeclContext(); dc; dc = dc->getLexicalParent()) {
        if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(dc)) {
            if (spec->getSpecializationKind() != clang::TSK_ExplicitSpecialization) return true;
        }
    }
    return false;
}

// eg. `void log(const char *f, __va_list_tag *ap)` becomes `void log(const char *f, __builtin_va_list ap)`
void respellDecayedVaList(std::string& text) {
    static const std::string tag = "__va_list_tag ";
    auto is_ident = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
    size_t pos = 0;
    while ((pos = text.find(tag, pos)) != std::string::npos) {
        // `struct __va_list_tag` in C, where the tag keyword is still printed
        size_t begin = pos;
        if (begin >= 7 && text.compare(begin - 7, 7, "struct ") == 0) begin -= 7;
        size_t cursor = pos + tag.size();
        std::string replacement = "__builtin_va_list";
        if (cursor < text.size() && text[cursor] == '*') {
            // decayed parameter type: `__va_list_tag *` or `__va_list_tag *ap`
            cursor++;
            if (cursor < text.size() && is_ident(text[cursor])) replacement += " ";
        } else {
            // canonical parameter type as written: `__va_list_tag ap[1]` or `__va_list_tag [1]`
            size_t name_end = cursor;
            while (name_end < text.size() && is_ident(text[name_end])) name_end++;
            if (text.compare(name_end, 3, "[1]") != 0) {
                pos = cursor;
                continue;
            }
            if (name_end > cursor) replacement += " " + text.substr(cursor, name_end - cursor);
            cursor = name_end + 3;
        }
        text.replace(begin, cursor - begin, replacement);
        pos = begin + replacement.size();
    }
}

// Recursively from the global namespace: a namespace adds its name unless unnamed or inline, a
// class adds itself as a type, and a transparent context, eg. `extern "C"`, adds nothing.
clang::NestedNameSpecifier buildQualifierForContext(clang::ASTContext& ctx, clang::DeclContext* dc) {
    dc = dc->getRedeclContext();
    if (dc->isTranslationUnit()) return clang::NestedNameSpecifier::getGlobal();
    clang::NestedNameSpecifier prefix = buildQualifierForContext(ctx, dc->getParent());
    if (auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(dc))
        return ns->isAnonymousNamespace() || ns->isInline() ? prefix : clang::NestedNameSpecifier(ctx, ns, prefix);
    if (auto* tag = llvm::dyn_cast<clang::TagDecl>(dc))
        return clang::NestedNameSpecifier(
            ctx.getTagType(clang::ElaboratedTypeKeyword::None, prefix, tag, false).getTypePtr());
    return prefix;
}

// Rewrites the AST in place, bottom up, then the caller prints it. Besides names (DeclRefExprs),
// it rewrites what prints a type as written: a value-initialization `T()` and a temporary `T{a}`,
// whose `T` may be a typedef of the scope the code came from.
void qualifyNonLocalNames(clang::Stmt*& node, clang::ASTContext& ctx) {
    if (!node) return;
    for (clang::Stmt*& child : node->children()) qualifyNonLocalNames(child, ctx);
    // the printer prints the syntactic form of braces, whose elements an instantiation leaves
    // unresolved, eg. the overload set `&impl<P>` of `{&impl<P>}`. It prints the resolved ones
    // of the semantic form instead, unless they are told apart by designators or brace elision.
    if (auto* list = llvm::dyn_cast<clang::InitListExpr>(node); list && list->getSyntacticForm()) {
        clang::InitListExpr* written = list->getSyntacticForm();
        bool same_elements = written->getNumInits() == list->getNumInits() &&
                             llvm::none_of(written->inits(), [](const clang::Expr* init) { return llvm::isa<clang::DesignatedInitExpr>(init); });
        for (unsigned i = 0; i < written->getNumInits(); ++i) {
            clang::Stmt* init = same_elements ? list->getInit(i) : written->getInit(i);
            if (!same_elements) qualifyNonLocalNames(init, ctx);
            written->setInit(i, llvm::cast<clang::Expr>(init));
        }
    }

    // a value-initialization, eg. `value_type()` of a default argument, would print the typedef
    // as written, so it becomes `((unsigned long)0)`, of the canonical type, found anywhere
    if (auto* init = llvm::dyn_cast<clang::CXXScalarValueInitExpr>(node); init && !init->getType()->isDependentType()) {
        clang::QualType type = init->getType().getCanonicalType();
        node = clang::CStyleCastExpr::Create(
            ctx, type, clang::VK_PRValue, clang::CK_NoOp,
            clang::IntegerLiteral::Create(ctx, llvm::APInt(ctx.getIntWidth(ctx.IntTy), 0), ctx.IntTy, init->getBeginLoc()), nullptr,
            clang::FPOptionsOverride(), ctx.getTrivialTypeSourceInfo(type), init->getBeginLoc(), init->getEndLoc());
        node = new (ctx) clang::ParenExpr(init->getBeginLoc(), init->getEndLoc(), llvm::cast<clang::Expr>(node));
        return;
    }
    // `Iter(p)` is printed with its canonical type, eg. `__gnu_cxx::__normal_iterator<int *, std::vector<int>>(p)`
    if (auto* temporary = llvm::dyn_cast<clang::CXXTemporaryObjectExpr>(node); temporary && !temporary->getType()->isDependentType()) {
        node = clang::CXXTemporaryObjectExpr::Create(
            ctx, temporary->getConstructor(), temporary->getType(),
            ctx.getTrivialTypeSourceInfo(temporary->getType().getCanonicalType()),
            llvm::ArrayRef(temporary->getArgs(), temporary->getNumArgs()), temporary->getParenOrBraceRange(),
            temporary->hadMultipleCandidates(), temporary->isListInitialization(), temporary->isStdInitListInitialization(),
            temporary->requiresZeroInitialization());
        return;
    }

    auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(node);
    clang::ValueDecl* decl = ref ? ref->getDecl() : nullptr;

    // a hidden friend is only found by its arguments, which it still is
    if (!llvm::isa_and_nonnull<clang::FunctionDecl, clang::VarDecl, clang::EnumConstantDecl>(decl) ||
        !decl->isDefinedOutsideFunctionOrMethod() ||
        llvm::all_of(decl->redecls(), [](const clang::Decl* redecl) {
            return redecl->getFriendObjectKind() != clang::Decl::FOK_None;
        }))
        return;
    // the same reference, with the qualifier from the global namespace, eg. `::std::min`
    clang::NestedNameSpecifierLocBuilder qualifier;
    qualifier.MakeTrivial(ctx, buildQualifierForContext(ctx, decl->getDeclContext()), ref->getSourceRange());
    clang::TemplateArgumentListInfo args;
    ref->copyTemplateArgumentsInto(args);
    node = clang::DeclRefExpr::Create(ctx, qualifier.getWithLocInContext(ctx), ref->getTemplateKeywordLoc(), decl,
                                      ref->refersToEnclosingVariableOrCapture(), ref->getNameInfo(), ref->getType(),
                                      ref->getValueKind(), ref->getFoundDecl(),
                                      ref->hasExplicitTemplateArgs() ? &args : nullptr, ref->isNonOdrUse());
}

namespace {

/// Replaces every `this` of a body with an expression, by rewriting the child pointers of their
/// parents, see replaceThis.
class ReplaceThisVisitor : public clang::RecursiveASTVisitor<ReplaceThisVisitor> {
  public:
    ReplaceThisVisitor(clang::Expr* replacement) : m_replacement(replacement) {}

    // a class defined in the body has a `this` of its own, eg. in `struct L { int g() { return x; } };`,
    // which stays
    bool TraverseCXXRecordDecl(clang::CXXRecordDecl*) { return true; }

    bool TraverseStmt(clang::Stmt* stmt) {
        if (!stmt) return true;
        if (!clang::RecursiveASTVisitor<ReplaceThisVisitor>::TraverseStmt(stmt)) return false;
        replaceThisInChildren(stmt);
        // braces, `{this, 1}`, have two forms with an array of elements each: the parents point at
        // the semantic one, but the visitor walks and the printer prints the syntactic one
        if (auto* ile = llvm::dyn_cast<clang::InitListExpr>(stmt))
            replaceThisInChildren(ile->getSyntacticForm());
        return true;
    }

  private:
    void replaceThisInChildren(clang::Stmt* stmt) {
        if (!stmt) return;
        for (clang::Stmt*& child : stmt->children()) {
            if (llvm::isa_and_nonnull<clang::CXXThisExpr>(child))
                child = m_replacement;
        }
    }

    clang::Expr* m_replacement;
};

}  // namespace

void replaceThis(clang::Stmt* node, clang::Expr* replacement) {
    ReplaceThisVisitor visitor(replacement);
    visitor.TraverseStmt(node);
}

// Walks up through what doesn't change how the value is used, parentheses, implicit casts and the
// cleanups of temporaries, to a MaterializeTemporaryExpr, which binds it to a reference or accesses
// a member of it, eg. `make().size()`, or to anything else, which uses the value itself.
bool isUsedByValue(clang::ASTContext& ctx, const clang::Expr* expr) {
    for (const clang::Stmt* child = expr;;) {
        auto parents = ctx.getParents(*child);
        const auto* parent = parents.empty() ? nullptr : parents[0].get<clang::Stmt>();
        if (!parent) return true;
        if (llvm::isa<clang::MaterializeTemporaryExpr>(parent)) return false;
        if (!llvm::isa<clang::ParenExpr, clang::ImplicitCastExpr, clang::CXXBindTemporaryExpr, clang::ExprWithCleanups>(parent))
            return true;
        child = parent;
    }
}

namespace {

// Whether @p s only wraps the expression under it, without being an operation written in the
// source, so that a call wrapped in it is still all of its statement:
//
//     ParenExpr                   `(f())`
//     ImplicitCastExpr            `const T t = f();`, the const added to the result
//     ExprWithCleanups            around an expression whose temporaries are destroyed at its end
//     CXXBindTemporaryExpr        the temporary of such a T
//     MaterializeTemporaryExpr    `const T &t = f();`, the result made an object to bind to
//     ExplicitCastExpr            `bool b = static_cast<bool>(obj);`, only around an explicit
//                                 conversion operator, which hoisting would write again, forever
bool isTransparentParent(const clang::Stmt* s, bool isExplicitCoversionOperator) {
    return llvm::isa<clang::ParenExpr,
                     clang::ImplicitCastExpr,
                     clang::ExprWithCleanups,
                     clang::CXXBindTemporaryExpr,
                     clang::MaterializeTemporaryExpr>(s) || 
           (isExplicitCoversionOperator ? 
            (llvm::isa<clang::ExplicitCastExpr>(s)) : false);
}

}  // namespace

// The statement @p call is all of already, directly in a block, or null. hoist-calls leaves such a
// call as it is, and the inliner pastes the inlined code before the statement:
//  - a declaration, `int x = f();`, gives its DeclStmt;
//  - an assignment, `x = f();`, also `a = f();` of a class, whose `=` is a call itself, gives the
//    assignment;
//  - a call returning void, `f();`, gives the call, or the expression around it that only wraps it.
// A call returning a value, `f();`, is hoisted all the same, as it may be the value of a
// statement expression, `({ ...; f(); })`. The cast around an explicit conversion operator,
// `bool b = static_cast<bool>(obj);`, is looked through: hoisting it would write the same cast
// again, and the pass would never stop.
const clang::Stmt* topLevelStatement(clang::ASTContext& ctx, const clang::CallExpr* call) {
    bool isExplicitCoversionOperator = false;
    if (auto* member_call_expr = llvm::dyn_cast<clang::CXXMemberCallExpr>(call)) {
        const auto* conversion_decl = llvm::dyn_cast_or_null<clang::CXXConversionDecl>(member_call_expr->getMethodDecl());
        if (conversion_decl && conversion_decl->isExplicit()) {
            isExplicitCoversionOperator = true;
        }
    }

    // the first parent that isn't transparent, eg. the VarDecl of `std::string s = make();`, past the
    // temporary's materialization and cleanups, and the expression under it
    const clang::Stmt* expr = call;
    clang::DynTypedNode parent_node;
    for (;;) {
        auto parents = ctx.getParents(*expr);
        if (parents.empty()) return nullptr;
        parent_node = parents[0];
        const auto* parent = parent_node.get<clang::Stmt>();
        if (!parent || !isTransparentParent(parent, isExplicitCoversionOperator)) break;
        expr = parent;
    }

    // int x = foo();
    if (const auto* var_decl = parent_node.get<clang::VarDecl>()) {
        auto vd_parents = ctx.getParents(*var_decl);
        if (!vd_parents.empty()) {
            if (const auto* decl_stmt = vd_parents[0].get<clang::DeclStmt>()) {
                auto ds_parents = ctx.getParents(*decl_stmt);
                if (!ds_parents.empty()) {
                    if (const auto* parent_stmt = ds_parents[0].get<clang::Stmt>()) {
                        if (llvm::isa<clang::CompoundStmt>(parent_stmt)) {
                            return decl_stmt;
                        }
                    }
                }
            }
        }
    } else if (const auto* parent_stmt = parent_node.get<clang::Stmt>()) {
        // x = foo();
        // a compound assignment, `x += foo();`, isn't one: hoist-calls makes it `int tmp = foo(); x += tmp;`
        if (const auto* bin_op = llvm::dyn_cast<clang::BinaryOperator>(parent_stmt)) {
            if (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Assign) {
                auto bo_parents = ctx.getParents(*bin_op);
                if (!bo_parents.empty()) {
                    if (const auto* grandparent = bo_parents[0].get<clang::Stmt>()) {
                        if (llvm::isa<clang::CompoundStmt>(grandparent)) {
                            return bin_op;
                        }
                    }
                }
            }
        // a = foo();  where a is a class type
        } else if (const auto* op_call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(parent_stmt)) {
            if (op_call->getOperator() == clang::OO_Equal) {
                auto oc_parents = ctx.getParents(*op_call);
                if (!oc_parents.empty()) {
                    if (const auto* grandparent = oc_parents[0].get<clang::Stmt>()) {
                        if (llvm::isa<clang::CompoundStmt>(grandparent)) {
                            return op_call;
                        }
                    }
                }
            }
        // foo();
        } else if (llvm::isa<clang::CompoundStmt>(parent_stmt)) {
            // a call returning a value is hoisted all the same, as it may be the value of a
            // statement expression, `int res = ({ ...; foo(); });`
            if (call->getType()->isVoidType())
                return expr;
        }
    }
    return nullptr;
}

}  // namespace invisible
