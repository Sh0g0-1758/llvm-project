#include "helpers.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Tooling/Tooling.h"

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
    // Suppress Clang's implicit builtin declarations so that hoisted
    // temporaries use the type names from the user's headers (e.g.
    // `size_t`) rather than Clang-internal typedefs (`__size_t`).
    args.emplace_back("-fno-builtin");
    // Suppress warnings that show up as errors while parsing using
    // libtooling but are otherwise benign as still compiles the TU. 
    // For eg. -Wincompatible-pointer-types
    args.emplace_back("-Wno-everything");
}

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

/// Renders the diagnostics of a clang run into a string instead of stderr.
void captureDiagnostics(clang::CompilerInstance& ci, llvm::raw_ostream& out) {
    ci.getDiagnosticOpts().ShowColors = settings().color;
    out.enable_colors(settings().color);
    ci.getDiagnostics().setClient(new clang::TextDiagnosticPrinter(out, ci.getDiagnosticOpts()), /*ShouldOwnClient=*/true);
    // clang prints the "N errors generated." summary there
    ci.setVerboseOutputStream(out);
}

class InvisibleConsumer : public clang::ASTConsumer {
  public:
    explicit InvisibleConsumer(TUCallback callback, clang::CompilerInstance& ci) : m_callback(std::move(callback)), m_ci(ci) {}

    void HandleTranslationUnit(clang::ASTContext& ctx) override { m_callback(ctx, m_ci.getSema()); }

  private:
    TUCallback m_callback;
    clang::CompilerInstance& m_ci;
};

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

std::string runInMemory(const std::string& source, const std::string& filename, std::vector<std::string>& args, TUCallback callback,
                        clang::Rewriter& rewriter) {
    std::string diagnostics;
    llvm::raw_string_ostream diagnostics_out(diagnostics);
    auto action = std::make_unique<InvisibleAction>(std::move(callback), diagnostics_out);
    if (!clang::tooling::runToolOnCodeWithArgs(std::move(action), source, args, filename))
        verbose() << "clang failed to parse " << filename << " while running a pass\n";
    verbose() << diagnostics;
    return getRewrittenSource(rewriter, source);
}

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

bool isInsideFunction(const clang::Decl* decl) {
    for (const clang::DeclContext* decl_context = decl->getLexicalDeclContext(); decl_context; decl_context = decl_context->getLexicalParent()) {
        if (llvm::isa<clang::FunctionDecl>(decl_context)) return true;
        if (llvm::isa<clang::BlockDecl>(decl_context)) return true;
    }
    return false;
}

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

bool isLeftForConstantEvaluation(const clang::FunctionDecl* func) {
    if (func->getDeclName().isIdentifier() && func->getName().starts_with("__invisible_clone_")) return true;
    if (const auto* kept = func->getAttr<clang::AnnotateAttr>())
        if (kept->getAnnotation() == "invisible_constant_evaluated") return true;
    const auto* tag = func->getNumParams() ? func->getParamDecl(0)->getType()->getAsRecordDecl() : nullptr;
    return tag && tag->getDeclName().isIdentifier() && tag->getName() == "__invisible_clone_tag";
}

bool isLeftAsWritten(const clang::Decl* decl) {
    if (const auto* func = llvm::dyn_cast_or_null<clang::FunctionDecl>(decl))
        if (isLeftForConstantEvaluation(func) || func->getFriendObjectKind() != clang::Decl::FOK_None) return true;
    // eg. the definition of a static data member of a class template
    if (const auto* var = llvm::dyn_cast_or_null<clang::VarDecl>(decl);
        var && (var->isTemplated() || var->getInstantiatedFromStaticDataMember()))
        return true;
    const auto* context = llvm::dyn_cast_or_null<clang::DeclContext>(decl);
    return context && context->isDependentContext();
}

bool isInstantiatedCopy(const clang::FunctionDecl* func) {
    if (func->isTemplateInstantiation()) return true;
    for (const clang::DeclContext* dc = func->getLexicalDeclContext(); dc; dc = dc->getLexicalParent()) {
        if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(dc)) {
            if (spec->getSpecializationKind() != clang::TSK_ExplicitSpecialization) return true;
        }
    }
    return false;
}

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

clang::NestedNameSpecifier buildQualifierForContext(clang::ASTContext& ctx, clang::DeclContext* dc) {
    if (!dc || dc->isTranslationUnit()) return clang::NestedNameSpecifier(std::nullopt);

    if (auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(dc))
        return clang::NestedNameSpecifier(ctx, ns, buildQualifierForContext(ctx, dc->getParent()));

    if (auto* rec = llvm::dyn_cast<clang::CXXRecordDecl>(dc))
        return clang::NestedNameSpecifier(
            ctx.getTypeDeclType(clang::ElaboratedTypeKeyword::None, std::nullopt, rec).getTypePtr());

    return clang::NestedNameSpecifier(std::nullopt);
}

// Fully qualifies all the declarations present in a given Stmt AST node in-place. 
void qualifyNonLocalNames(clang::Stmt*& node, clang::ASTContext& ctx) {
    if (!node) return;

    if (auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(node)) {
        clang::DeclContext* context = ref->getDecl()->getDeclContext()->getRedeclContext();
        if (!ref->hasQualifier() && !context->isFunctionOrMethod()) {
            clang::NestedNameSpecifier qualifier = buildQualifierForContext(ctx, context);
            if (qualifier) {
                clang::NestedNameSpecifierLocBuilder builder;
                builder.MakeTrivial(ctx, qualifier, ref->getSourceRange());
                node = clang::DeclRefExpr::Create(
                    ctx, builder.getWithLocInContext(ctx), clang::SourceLocation(),
                    ref->getDecl(), false, ref->getLocation(), ref->getType(), ref->getValueKind());
            }
        }
        return;
    }

    for (clang::Stmt*& child : node->children())
        qualifyNonLocalNames(child, ctx);
}

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

}  // namespace invisible
