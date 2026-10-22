#include "preprocess/decl_before_def/DeclBeforeDefVisitor.h"
#include "helpers.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/JSON.h"
#include "clang/AST/TemplateBase.h"
#include "clang/Sema/TemplateDeduction.h"

#include <string>
#include <queue>
#include <stack>

namespace invisible {

namespace {

// The position of the `>` that closes the template argument list opened by the `<` at @p open in
// @p text, or npos, eg. of the last `>` in `f<pair<int, char>>(p)`. Parentheses are counted too,
// so that the `>` of `f<(a > b)>` isn't taken for the end.
size_t closingAngle(const std::string& text, size_t open) {
    int depth = 0;
    for (size_t at = open; at < text.size(); ++at) {
        if (text[at] == '<' || text[at] == '(') ++depth;
        else if ((text[at] == '>' || text[at] == ')') && --depth == 0) return at;
    }
    return std::string::npos;
}

// The printer puts a `[[...]]` written after the name of a function, eg.
// `operator() [[nodiscard]] (int)`, after its type, where it would apply to the
// type. In front of the declaration it applies to the function. When a requires
// clause follows the template heads, the attributes are dropped. They are eg.
// `nodiscard` or `deprecated`, which only matter for diagnostics.
//
//     template<> int S::operator()(int) [[nodiscard]]     (printed, an error)
//     template<> [[nodiscard]] int S::operator()(int)     (after the template heads)
void hoistTrailingAttributes(std::string& decl) {
    size_t end = decl.find_last_not_of(" \n");
    if (end == std::string::npos || end < 1 || decl.compare(end - 1, 2, "]]") != 0) return;
    size_t begin = decl.rfind("[[", end);
    if (begin == std::string::npos) return;
    std::string attributes = decl.substr(begin, end + 1 - begin);
    while (begin > 0 && decl[begin - 1] == ' ') --begin;
    decl.erase(begin, end + 1 - begin);
    // after the template heads
    size_t at = 0;
    while (decl.compare(at, 8, "template") == 0) {
        size_t open = decl.find_first_not_of(' ', at + 8);
        if (open == std::string::npos || decl[open] != '<') break;
        size_t close = closingAngle(decl, open);
        if (close == std::string::npos) return;
        at = decl.find_first_not_of(" \n", close + 1);
        if (at == std::string::npos) return;
    }
    if (decl.compare(at, 8, "requires") != 0) decl.insert(at, attributes + " ");
}

// Removes the template arguments of the function @p name in the declaration @p decl, those
// between `name<` and the `>` before its parameters, eg. `get<int>(int)` becomes `get(int)`.
// Written arguments are substituted into every template of that name, which can be a hard error.
// Left out, they are deduced from the type, see VisitFunctionDecl. Only `name<...>(` is matched,
// not eg. `vector<get<int>>` in a parameter type.
void stripTemplateArguments(std::string& decl, const std::string& name) {
    auto is_word = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
    for (size_t at = decl.find(name + "<"); at != std::string::npos; at = decl.find(name + "<", at + 1)) {
        if (at > 0 && is_word(decl[at - 1])) continue;
        size_t open = at + name.size();
        size_t close = closingAngle(decl, open);
        if (close != std::string::npos && close + 1 < decl.size() && decl[close + 1] == '(') {
            decl.erase(open, close + 1 - open);
            return;
        }
    }
}

// Puts the deferred @p text of @p func, a function in an unnamed namespace, back in that
// namespace: declared outside it, it would be another function. `(anonymous namespace)::`,
// which isn't C++, is removed from the names, which are still found through the namespace.
// eg. for `f` in `namespace a { namespace { struct T; void f(T); } }` the deferred
// `void a::(anonymous namespace)::f(a::(anonymous namespace)::T)` becomes
// `namespace a { namespace { void f(a::T) } }`.
void enterUnnamedNamespace(const clang::FunctionDecl* func, std::string& text) {
    static const std::string unnamed = "(anonymous namespace)::";
    for (size_t pos; (pos = text.find(unnamed)) != std::string::npos;) text.erase(pos, unnamed.size());
    std::vector<const clang::NamespaceDecl*> path;
    for (const clang::DeclContext* dc = func->getDeclContext(); dc; dc = dc->getParent())
        if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(dc)) path.insert(path.begin(), ns);
    auto innermost = std::find_if(path.rbegin(), path.rend(), [](const clang::NamespaceDecl* ns) { return ns->isAnonymousNamespace(); });
    if (innermost == path.rend()) return;
    std::string open, close, outer;
    for (auto it = path.begin(); it != innermost.base(); ++it) {
        open += (*it)->isAnonymousNamespace() ? "namespace { " : "namespace " + (*it)->getName().str() + " { ";
        close += " }";
        if (!(*it)->isAnonymousNamespace()) outer += (*it)->getName().str() + "::";
    }
    std::string qualified = func->getQualifiedNameAsString();
    for (size_t pos; (pos = qualified.find(unnamed)) != std::string::npos;) qualified.erase(pos, unnamed.size());
    if (size_t pos = text.find(qualified); !outer.empty() && pos != std::string::npos) text.erase(pos, outer.size());
    text = open + text + close;
}

// Makes the definition @p decl `inline`, and `used` if @p used, after its `template<>` heads and attributes,
// eg. `template<> int twice<int>(int x){...}` becomes `template<> [[gnu::used]] inline int twice<int>(int x){...}`.
// What was implicitly inline, eg. a member defined in its class or an instantiation, may be defined in
// every TU. `inline` avoids duplicate definitions at link time. `used` still emits each one, for the
// optimization records.
void inlineUsed(std::string& decl, bool used) {
    size_t at = 0;
    while (decl.compare(at, 8, "template") == 0) {
        size_t close = decl.find('>', at);
        if (close == std::string::npos || decl.find_first_not_of(" <", at + 8) != close) break;
        at = decl.find_first_not_of(" \n", close + 1);
    }
    if (used) {
        decl.insert(at, "[[gnu::used]] ");
        at += 14;
    }
    while (decl.compare(at, 2, "[[") == 0 || decl.compare(at, 13, "__attribute__") == 0) {
        int depth = 0;
        for (; at < decl.size(); ++at) {
            if (decl[at] == '[' || decl[at] == '(') ++depth;
            else if ((decl[at] == ']' || decl[at] == ')') && --depth == 0 && decl[at + 1] != ']' && decl[at + 1] != ')') break;
        }
        at = decl.find_first_not_of(" \n", at + 1);
    }
    decl.insert(at, "inline ");
}

// Whether DeclBeforeDef leaves @p func where it is written, with its
// instantiations: a defaulted or deleted function, one used by what stays in
// place, a friend, and a template specialized for the closure type of a lambda,
// eg. `template <class F> void call(F f)` called as `call([]{})`: `call<(lambda)>` has no
// name a specialization could be declared with, so `call` keeps its body where it is.
bool isKeptByDeclBeforeDef(const clang::FunctionDecl* func, const std::set<const clang::FunctionDecl*>& kept_in_place) {
    // a consteval function is only evaluated by the compiler, which needs its definition before it is used
    if (func->isDefaulted() || func->isDeleted() || func->isConsteval()) return true;
    if (const clang::FunctionDecl* pattern = func->getTemplateInstantiationPattern()) func = pattern;
    if (kept_in_place.count(func->getCanonicalDecl()) || func->getFriendObjectKind() != clang::Decl::FOK_None) return true;
    // a specialization for the closure type of a lambda can't be named
    const clang::FunctionTemplateDecl* tmpl = func->getDescribedFunctionTemplate();
    auto names_lambda = [](const clang::TemplateArgument& arg) {
        const clang::CXXRecordDecl* record =
            arg.getKind() == clang::TemplateArgument::Type ? arg.getAsType().getNonReferenceType()->getAsCXXRecordDecl() : nullptr;
        return record && record->isLambda();
    };
    return tmpl && llvm::any_of(tmpl->specializations(), [&](const clang::FunctionDecl* spec) {
        return llvm::any_of(spec->getTemplateSpecializationArgs()->asArray(), [&](const clang::TemplateArgument& arg) {
            return arg.getKind() == clang::TemplateArgument::Pack ? llvm::any_of(arg.pack_elements(), names_lambda)
                                                                   : names_lambda(arg);
        });
    });
}

// An expression naming @p func, the specialization of a member of a class template,
// eg. `&S<int>::f` or `(S<int>(static_cast<int (*)()>(nullptr)()))`. Put in the
// declaration of the specialization, it makes clang compute the member's exception
// specification first. Otherwise a computed `noexcept` doesn't match: the declaration
// isn't taken as a specialization of the member, or is rejected when it has `requires`.
std::string printMemberProbe(clang::ASTContext& ctx, const clang::FunctionDecl* func, const clang::PrintingPolicy& policy) {
    const auto* method = llvm::cast<clang::CXXMethodDecl>(func);
    const clang::CXXRecordDecl* record = method->getParent();
    std::string cls;
    llvm::raw_string_ostream cls_out(cls);
    record->getNameForDiagnostic(cls_out, policy, true);
    // a value of the exact type of each parameter, without constructing any
    if (const auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(method)) {
        std::string probe = "(" + cls + "(";
        for (unsigned i = 0; i < ctor->getNumParams(); ++i) {
            if (i > 0) probe += ", ";
            std::string maker;
            llvm::raw_string_ostream maker_out(maker);
            ctx.getFunctionType(ctor->getParamDecl(i)->getType(), {}, {}).print(maker_out, policy, "*");
            probe += "static_cast<" + maker + ">(nullptr)()";
        }
        return probe + "))";
    }
    if (llvm::isa<clang::CXXDestructorDecl>(method))
        return "(static_cast<" + cls + "*>(nullptr)->~" + record->getNameAsString() + "(), 0)";
    std::string name;
    llvm::raw_string_ostream name_out(name);
    method->getDeclName().print(name_out, policy);
    std::string address = "&" + cls + "::" + name;
    if (record->lookup(method->getDeclName()).isSingleResult()) return address;
    // one of the overloads, by its type without the exception specification
    const auto* proto = method->getType()->castAs<clang::FunctionProtoType>();
    clang::FunctionProtoType::ExtProtoInfo info = proto->getExtProtoInfo();
    info.ExceptionSpec = {};
    clang::QualType type = ctx.getFunctionType(proto->getReturnType(), proto->getParamTypes(), info);
    std::string pointer;
    llvm::raw_string_ostream pointer_out(pointer);
    type.print(pointer_out, policy, method->isImplicitObjectMemberFunction() ? cls + "::*" : "*");
    return "static_cast<" + pointer + ">(" + address + ")";
}

// The class types of the results used by value, see instantiateConstructorsOfValues, eg. the
// `std::string` of `use(make_name())`, which hoist-calls will move into a temporary.
class ValueTypeCollector : public clang::RecursiveASTVisitor<ValueTypeCollector> {
  public:
    explicit ValueTypeCollector(clang::ASTContext& ctx) : m_ctx(ctx) {}
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool VisitCallExpr(clang::CallExpr* call) {
        clang::QualType type = call->getType();
        if (!call->isPRValue() || type->isDependentType() || !type->getAsCXXRecordDecl()) return true;
        if (isUsedByValue(m_ctx, call)) m_values.push_back({call->getBeginLoc(), type});
        return true;
    }
    std::vector<std::pair<clang::SourceLocation, clang::QualType>> m_values;

  private:
    clang::ASTContext& m_ctx;
};

}  // namespace

// only an instantiation can fail to be constant: written code already compiled as written
bool printsConstexpr(const clang::FunctionDecl* func) {
    if (!func->isConstexpr() || func->isConsteval() || !isInstantiatedCopy(func)) return func->isConstexpr();
    llvm::SmallVector<clang::PartialDiagnosticAt, 8> notes;
    return clang::Expr::isPotentialConstantExpr(func, notes);
}

// eg. hoist-calls turns `use(make<int>())` into `Box<int> tmp = make<int>(); use(std::move(tmp));`,
// which calls `Box<int>(Box<int>&&)`: an instantiated move constructor the input never needed.
void instantiateConstructorsOfValues(clang::ASTContext& ctx, clang::Sema& sema) {
    if (!ctx.getLangOpts().CPlusPlus) return;
    // looking the constructors up declares the implicit ones, which isn't done while
    // traversing the classes they are declared in
    ValueTypeCollector collector(ctx);
    collector.TraverseDecl(ctx.getTranslationUnitDecl());
    for (const auto& [loc, type] : collector.m_values) {
        auto* record = type->getAsCXXRecordDecl();
        if (!record->hasDefinition()) continue;
        unsigned quals = type.getCVRQualifiers();
        clang::CXXConstructorDecl* ctor = sema.LookupMovingConstructor(record, quals);
        if (!ctor || ctor->isDeleted()) ctor = sema.LookupCopyingConstructor(record, quals);
        if (ctor && !ctor->isDeleted() && !ctor->isTrivial()) sema.MarkFunctionReferenced(loc, ctor);
    }
    sema.PerformPendingInstantiations();
}

// eg. for `abs<int>` of `template <class T> T abs(T)` and `template <class T> T abs(complex<T>)`,
// the type `int(int)` is deduced by the first template only: `template<> int abs(int)` declares it.
unsigned countDeducedTemplates(clang::Sema& sema, clang::FunctionDecl* func, bool with_arguments) {
    const clang::FunctionTemplateDecl* primary = func->getPrimaryTemplate();
    clang::TemplateArgumentListInfo arguments;
    if (with_arguments)
        for (const clang::TemplateArgument& arg : func->getTemplateSpecializationArgs()->asArray())
            arguments.addArgument(sema.getTrivialTemplateArgumentLoc(arg, clang::QualType(), func->getLocation()));
    unsigned deduced = 0;
    for (clang::NamedDecl* found : primary->getDeclContext()->getRedeclContext()->lookup(func->getDeclName())) {
        auto* candidate = llvm::dyn_cast<clang::FunctionTemplateDecl>(found->getUnderlyingDecl());
        clang::FunctionDecl* specialization = nullptr;
        clang::sema::TemplateDeductionInfo info(func->getLocation());
        clang::Sema::SFINAETrap trap(sema);
        if (candidate && sema.DeduceTemplateArguments(candidate, with_arguments ? &arguments : nullptr, func->getType(),
                                                      specialization, info) == clang::TemplateDeductionResult::Success)
            ++deduced;
    }
    return deduced;
}

// --- DeclBeforeDefVisitor ----------------------------------------------

// the instantiations are what is deferred as specializations, or copied
bool DeclBeforeDefVisitor::shouldVisitTemplateInstantiations() const { return true; }

DeclBeforeDefVisitor::DeclBeforeDefVisitor(clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter,
                                           const std::set<const clang::FunctionDecl*>& kept_in_place,
                                           const std::set<std::string>& dropped,
                                           const std::set<const clang::FunctionDecl*>& specialized)
    : m_ctx(ctx),
      m_sema(sema),
      m_rewriter(rewriter),
      m_kept_in_place(kept_in_place),
      m_specialized(specialized),
      m_policy(codePolicy(ctx)),
      m_deferred_policy_ctor(codePolicy(ctx)),
      m_deferred_policy_decl(codePolicy(ctx)),
      m_inplace_policy(codePolicy(ctx)),
      m_copy_policy(codePolicy(ctx)),
      m_names(ctx),
      m_dropped(dropped),
      m_mono(ctx.getLangOpts().CPlusPlus),
      m_custom_printer(ctx, m_policy, m_copies) {
    // the bodies of what is deferred, as written
    if (m_ctx.getLangOpts().CPlusPlus) m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;

    // the member initializers of a deferred constructor, at the end of the TU, where a type its
    // class declares, eg. `iterator` of `iterator it_;`, is only found qualified
    m_deferred_policy_ctor = m_policy;
    m_deferred_policy_ctor.PrintAsCanonical = true;
    m_deferred_policy_ctor.FullyQualifiedName = true;

    // the deferred declarations and the heads of the deferred definitions: a type defined in a
    // class, eg. `size_type` of `vector<int>::size()`, is printed canonically and fully
    // qualified, `unsigned long`, as the head is outside the class
    m_deferred_policy_decl = m_policy;
    m_deferred_policy_decl.PrintAsCanonical = true;
    m_deferred_policy_decl.FullyQualifiedName = true;
    m_deferred_policy_decl.TerseOutput = true;
    // a specialization or redeclaration can't repeat the default arguments, it
    // takes them from the declaration they were written on, which is kept.
    m_deferred_policy_decl.SuppressInitializers = true;

    // the declarations written in place of the definitions moved to the end, eg. `int f(int x);`
    // where `int f(int x) { ... }` was
    m_inplace_policy = m_policy;
    m_inplace_policy.TerseOutput = true;
    m_inplace_policy.SuppressTemplateArgsInCXXConstructors = true;

    // a copy is printed at the end, from the global namespace, eg. `std::vector<int> *__this`
    // rather than `vector<int> *this` as written in namespace std
    m_copy_policy = m_policy;
    m_copy_policy.PrintAsCanonical = true;
    m_copy_policy.SuppressUnwrittenScope = true;

    // which functions are copied, and the names of their copies, are known before any is printed
    if (m_mono) monomorphize();
}

// replaces the template argument list after the name of the specialization @p func in @p str with
// the one printed from its arguments, eg. the printer writes the declaration of `f<int, char>` of
// `template <class... Ts> void f(Ts...)` as `f<Ts...>` or `f<>`; it becomes `f<int, char>`.
void DeclBeforeDefVisitor::fixParameterPack(std::string& str, clang::FunctionDecl* func) {
    const clang::TemplateArgumentList* args = func->getTemplateSpecializationArgs();
    if (!args) return;

    std::string correct;
    llvm::raw_string_ostream correct_out(correct);
    clang::printTemplateArgumentList(correct_out, args->asArray(), m_deferred_policy_decl);

    const std::string name = func->getNameAsString();
    const size_t name_pos = str.find(name + "<");
    if (name_pos == std::string::npos) return;

    const size_t open = name_pos + name.size();
    const size_t close = closingAngle(str, open);
    if (close == std::string::npos) return;

    str.replace(open, close + 1 - open, correct);
}

// removes the template argument list following the constructor's name, ie. the
// `<...>` in `::Name<...>(`. The class's own template arguments in the qualifier
// (`Name<...>::`) are followed by `::` instead and are left untouched, eg.
// `template<> Box<int>::Box<int>(int)` (not C++) becomes `template<> Box<int>::Box(int)`.
void DeclBeforeDefVisitor::stripConstructorTemplateArgs(std::string& str, clang::CXXConstructorDecl* ctor) {
    const std::string name = "::" + ctor->getParent()->getNameAsString() + "<";
    for (size_t pos = str.find(name); pos != std::string::npos; pos = str.find(name, pos + 1)) {
        const size_t open = pos + name.size() - 1;
        const size_t close = closingAngle(str, open);
        if (close != std::string::npos && close + 1 < str.size() && str[close + 1] == '(') {
            str.erase(open, close + 1 - open);
            return;
        }
    }
}

// removes the exception specification following a destructor's empty parameter
// list, ie. the ` noexcept` or ` noexcept(...)` in `~Name() noexcept`. A destructor's is
// implicit and computed from its members; the one clang prints may not match what it computes
// for the redeclaration, eg. `noexcept(false)` of a member whose destructor may throw.
void DeclBeforeDefVisitor::stripDestructorExceptionSpec(std::string& str) {
    const std::string spec = "() noexcept";
    const size_t pos = str.find(spec);
    if (pos == std::string::npos) return;
    const size_t begin = pos + 2;
    size_t end = pos + spec.size();
    if (end < str.size() && str[end] == '(') {
        for (size_t depth = 0; end < str.size(); ++end) {
            if (str[end] == '(') ++depth;
            else if (str[end] == ')' && --depth == 0) { ++end; break; }
        }
    }
    str.erase(begin, end - begin);
}

// how many `template<>` heads a member of an instantiated class template needs before it, one per
// enclosing instantiated class, eg. 2 for `Outer<int>::Inner<char>::f()`, which is declared as
// `template<> template<> void Outer<int>::Inner<char>::f()`
unsigned DeclBeforeDefVisitor::countEnclosingTemplateLevels(const clang::FunctionDecl* function_decl) {
    // an explicit specialization is printed with its own `template<>` heads
    if (function_decl->getTemplateSpecializationKind() == clang::TSK_ExplicitSpecialization) return 0;
    unsigned levels = 0;
    // a class explicitly specialized in the TU is an ordinary class, `template<> struct S<int> { ... };`
    for (const clang::DeclContext* dc = function_decl->getDeclContext(); dc; dc = dc->getParent())
        if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(dc);
            spec && spec->getSpecializationKind() != clang::TSK_ExplicitSpecialization)
            ++levels;
    return levels;
}

// strip specifiers that are legal only inside the class definition, eg. a deferred
// `static int S::count()` or `virtual void S::draw()` becomes `int S::count()` / `void S::draw()`
void DeclBeforeDefVisitor::stripRecordDeclOnlySpecifiers(std::string& s) {
    auto isWordChar = [](char c) { return isalnum((unsigned char)c) || c == '_'; };

    for (llvm::StringRef kw : {"static ", "virtual "}) {
        size_t pos = 0;
        while ((pos = s.find(kw.str(), pos)) != std::string::npos) {
            if (pos > 0 && isWordChar(s[pos - 1])) {
                ++pos;
                continue;
            }
            s.erase(pos, kw.size());
        }
    }

    // `explicit` and `explicit(cond)`, eg. `explicit(!is_convertible_v<U, T>) S::S(U)`
    size_t pos = 0;
    while ((pos = s.find("explicit", pos)) != std::string::npos) {
        if (pos > 0 && isWordChar(s[pos - 1])) {
            ++pos;
            continue;
        }
        size_t i = pos + 8;
        if (i < s.size() && s[i] == '(') {
            for (size_t depth = 0; i < s.size(); ++i) {
                if (s[i] == '(') ++depth;
                else if (s[i] == ')' && --depth == 0) { ++i; break; }
            }
        } else if (i < s.size() && isWordChar(s[i])) {
            ++pos;
            continue;
        }
        while (i < s.size() && s[i] == ' ') ++i;
        s.erase(pos, i - pos);
    }

    // the trailing pure, override and final, eg. `void S::draw() override`. The list is scanned
    // once, so `override` is listed again after `final` for `f() override final`.
    for (llvm::StringRef kw : {" = 0", " override", " final", " override"})
        if (llvm::StringRef(s).ends_with(kw)) s.resize(s.size() - kw.size());
}

// The member initializer list of a deferred constructor, eg. ` : data_(new int[n]), size_(0)`.
// The printer prints it with neither the head nor the body, so it is printed here. Only the
// written initializers are printed, and default arguments are left out.
std::string DeclBeforeDefVisitor::printCtorInitializers(clang::CXXConstructorDecl* ctor) {
    std::string result;
    llvm::raw_string_ostream out(result);
    const char* separator = " : ";
    for (const clang::CXXCtorInitializer* init : ctor->inits()) {
        if (init->isInClassMemberInitializer() || !init->isWritten()) continue;
        out << separator;
        separator = ", ";

        if (init->isAnyMemberInitializer())
            out << init->getAnyMember()->getName();
        else if (init->isDelegatingInitializer())
            out << ctor->getNameAsString();
        else
            out << clang::QualType(init->getBaseClass(), 0).getAsString(m_deferred_policy_ctor);

        if (clang::Expr* expr = init->getInit()) {
            bool out_parens = !llvm::isa<clang::InitListExpr>(expr);
            if (out_parens) out << "(";

            if (auto* cleanups = llvm::dyn_cast<clang::ExprWithCleanups>(expr))
                expr = cleanups->getSubExpr();
            expr = expr->IgnoreParens();

            clang::Expr* simple_init = nullptr;
            clang::Expr** args = nullptr;
            unsigned num_args = 0;

            // `m(a, b)` is a constructor call or, in a template, a list of expressions; their
            // arguments are printed rather than the construction, `m(T(a, b))` would convert
            if (auto* paren_list = llvm::dyn_cast<clang::ParenListExpr>(expr)) {
                args = paren_list->getExprs();
                num_args = paren_list->getNumExprs();
            } else if (auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(expr)) {
                args = construct->getArgs();
                num_args = construct->getNumArgs();
            } else {
                simple_init = expr;
            }

            if (simple_init) {
                simple_init->printPretty(out, &m_custom_printer, m_deferred_policy_ctor);
            } else {
                for (unsigned i = 0; i != num_args; ++i) {
                    if (llvm::isa<clang::CXXDefaultArgExpr>(args[i])) break;
                    if (i) out << ", ";
                    args[i]->printPretty(out, &m_custom_printer, m_deferred_policy_ctor);
                }
            }

            if (out_parens) out << ")";
        } else {
            out << "()";
        }

        if (init->isPackExpansion()) out << "...";
    }
    return result;
}

// Records the definition of @p func to write at the end of the TU, and its declaration to write
// before every definition, from the heads @p definition_head and @p declaration_head that
// VisitFunctionDecl printed, eg. for `int twice(int x) { return x + x; }`:
//
//     int twice(int x);                                   (declaration, before every definition)
//     int twice(int x){ return x + x; }                   (definition, at the end)
//
// and for the instantiation `vector<int>::size()`:
//
//     template<> unsigned long std::vector<int>::size() const;
//     template<> [[gnu::used]] inline unsigned long std::vector<int>::size() const{ ... }
void DeclBeforeDefVisitor::recordFunctionDefinition(clang::FunctionDecl* func, std::string& definition_head,
                                                    std::string& declaration_head) {
    // a member of `extern template class A<int>;` is instantiated in another TU, and a
    // specialization of it after that declaration is an error
    if (func->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration) return;

    // the body isn't fully qualified like the head, which would break eg. a function redeclared in a block
    std::string body;
    llvm::raw_string_ostream body_out(body);
    // the body calls the copies, see redirectCalls
    if (m_mono) {
        clang::Stmt* stmt = func->getBody();
        redirectCalls(stmt);
    }
    func->getBody()->printPretty(body_out, &m_custom_printer, m_policy);

    // the printer writes one `template<>`, a member needs one per enclosing instantiated class
    std::string prefix;
    for (unsigned levels = countEnclosingTemplateLevels(func); levels > 0; --levels) prefix += "template<> ";

    // the printer prints neither the member initializers with the head nor with the body
    std::string ctor_init;
    if (auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(func)) ctor_init = printCtorInitializers(ctor);

    for (std::string* head : {&definition_head, &declaration_head}) {
        // a constructor template's arguments are always deduced and can't be written, which also
        // covers parameter packs; another template's pack is printed `<>`, see fixParameterPack
        if (auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(func); ctor && ctor->getPrimaryTemplate())
            stripConstructorTemplateArgs(*head, ctor);
        else if (const clang::FunctionTemplateDecl* primary = func->getPrimaryTemplate();
                 primary && primary->getTemplateParameters()->hasParameterPack())
            fixParameterPack(*head, func);

        // the printer spells out the implicit `noexcept` of a destructor, which a specialization
        // can't add when the destructor of its template has no written exception specification
        if (auto* dtor = llvm::dyn_cast<clang::CXXDestructorDecl>(func)) {
            const clang::FunctionDecl* pattern = dtor->getTemplateInstantiationPattern();
            if (pattern && pattern->getExceptionSpecSourceRange().isInvalid()) stripDestructorExceptionSpec(*head);
        }

        // `static`, `virtual`, `explicit`, `override` and `final` only go inside the class
        if (m_ctx.getLangOpts().CPlusPlus && llvm::isa<clang::CXXMethodDecl>(func)) stripRecordDeclOnlySpecifiers(*head);
    }

    // what was implicitly inline may be defined in every TU, so it stays inline to link. It is
    // also made used so that it is still emitted. Not in __cxxabiv1::__fundamental_type_info:
    // emitting its vtable makes clang define the typeinfo of every fundamental type, which
    // libstdc++ already does.
    std::string definition = prefix + definition_head;
    const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(func->getDeclContext());
    bool fundamental_type_info = record && record->getName() == "__fundamental_type_info" &&
                                 record->getDeclContext()->isNamespace() &&
                                 llvm::cast<clang::NamespaceDecl>(record->getDeclContext())->getName() == "__cxxabiv1";
    if (m_ctx.getLangOpts().CPlusPlus && (func->isInlined() || isInstantiatedCopy(func)) && !func->isInlineSpecified() &&
        !func->isConstexpr())
        inlineUsed(definition, !fundamental_type_info);
    definition += ctor_init + body;
    std::string declaration = prefix + declaration_head + ";";
    // what is in an unnamed namespace is declared and defined in it again
    enterUnnamedNamespace(func, definition);
    enterUnnamedNamespace(func, declaration);
    m_written.push_back({false, m_deferred_definitions.size()});
    m_deferred_definitions.push_back(definition);
    m_deferred_functions.push_back(func);
    m_deferred_keys.push_back(m_names.getName(func));
    m_deferred_declarations.push_back(declaration);
}

// The declarations of a K&R C definition, eg. `int add(a, b) int a; int b; { ... }`, which the
// printer prints in the old style that isn't a prototype: both are printed as prototypes,
// `int add(int a, int b)`, @p decl where the definition was and @p head at the end.
void DeclBeforeDefVisitor::getKRFunctionDeclarations(clang::FunctionDecl* func, std::string& decl, std::string& head) {
    llvm::raw_string_ostream decl_out(decl);
    llvm::raw_string_ostream head_out(head);

    std::string proto = func->getNameAsString();
    proto += "(";
    for (unsigned i = 0; i < func->getNumParams(); ++i) {
        if (i > 0) proto += ", ";
        clang::ParmVarDecl* param_decl = func->getParamDecl(i);
        std::string param;
        llvm::raw_string_ostream param_out(param);
        param_decl->getType().print(param_out, m_inplace_policy, param_decl->getNameAsString());
        proto += param;
    }
    proto += ")";

    func->getReturnType().print(decl_out, m_inplace_policy, proto);
    func->getReturnType().print(head_out, m_deferred_policy_decl, proto);
}

// Clang copies a default argument onto every redeclaration that inherits it and
// the printer would print it on all of them. Hides the inherited ones while the
// declaration is printed, so that each default only appears where it was written, eg.
// `void f(int x = 1); void f(int x) {...}` keeps `= 1` on the first only: the declaration written
// in place of the definition, `void f(int x = 1);`, would redefine the default argument.
class InheritedDefaultArgsHider {
  public:
    explicit InheritedDefaultArgsHider(clang::FunctionDecl* func) {
        for (clang::ParmVarDecl* param : func->parameters()) {
            if (!param->hasInheritedDefaultArg()) continue;
            bool uninstantiated = param->hasUninstantiatedDefaultArg();
            clang::Expr* arg = uninstantiated ? param->getUninstantiatedDefaultArg() : param->getInit();
            m_hidden.push_back({param, arg, uninstantiated});
            param->setDefaultArg(nullptr);
        }
        // and so are default template arguments, eg. on an out-of-line member template
        if (clang::FunctionTemplateDecl* tmpl = func->getDescribedFunctionTemplate()) {
            for (clang::NamedDecl* param : *tmpl->getTemplateParameters()) {
                if (auto* type = llvm::dyn_cast<clang::TemplateTypeParmDecl>(param)) hide(type);
                else if (auto* value = llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(param)) hide(value);
                else if (auto* templ = llvm::dyn_cast<clang::TemplateTemplateParmDecl>(param)) hide(templ);
            }
        }
    }

    ~InheritedDefaultArgsHider() {
        for (const auto& [param, arg, uninstantiated] : m_hidden) {
            if (uninstantiated) param->setUninstantiatedDefaultArg(arg);
            else param->setDefaultArg(arg);
        }
        for (const auto& restore : m_restores) restore();
    }

  private:
    template <typename Param> void hide(Param* param) {
        if (!param->defaultArgumentWasInherited()) return;
        auto* from = const_cast<Param*>(param->getDefaultArgStorage().getInheritedFrom());
        param->removeDefaultArgument();
        m_restores.push_back([param, from] { param->setInheritedDefaultArgument(param->getASTContext(), from); });
    }

    struct Hidden { clang::ParmVarDecl* param; clang::Expr* arg; bool uninstantiated; };
    std::vector<Hidden> m_hidden;
    std::vector<std::function<void()>> m_restores;
};

// clang prints the name of a conversion without the scope of its type, eg. `operator
// basic_string_view` or `operator unique_ptr<T>`, which is not a type once the class is a
// template specialization or out of its scope. The conversion is named after its type while printed.
// The destructor of a member class template is named with the template arguments of
// both classes, eg. `~_Impl<A...>::_Impl<B...>`, and is named `~_Impl` instead.
class ConversionNameSpeller {
  public:
    ConversionNameSpeller(clang::FunctionDecl* func, const clang::PrintingPolicy& policy)
        : m_func(func), m_saved(func->getDeclName()) {
        std::string name;
        if (auto* conversion = llvm::dyn_cast<clang::CXXConversionDecl>(func);
            conversion && !conversion->getConversionType()->isBuiltinType())
            name = "operator " + conversion->getConversionType().getAsString(policy);
        if (auto* dtor = llvm::dyn_cast<clang::CXXDestructorDecl>(func);
            dtor && dtor->getParent()->getIdentifier() && dtor->getParent()->isDependentContext() &&
            dtor->getParent()->getDeclContext()->isRecord())
            name = "~" + dtor->getParent()->getName().str();
        if (!name.empty()) func->setDeclName(clang::DeclarationName(&func->getASTContext().Idents.get(name)));
    }
    ~ConversionNameSpeller() { m_func->setDeclName(m_saved); }

  private:
    clang::FunctionDecl* m_func;
    clang::DeclarationName m_saved;
};

// Has @p func printed without `constexpr` while it is in scope, see printsConstexpr. Only a
// specialization of a function template may drop `constexpr`. A member of a class template is
// redeclared and must match. eg. `make_pair<const string, int>` of a `constexpr` template is
// declared `template<> pair<const string, int> make_pair(...)`.
class ConstexprDropper {
  public:
    explicit ConstexprDropper(clang::FunctionDecl* func) : m_func(func), m_saved(func->getConstexprKind()) {
        if (func->getPrimaryTemplate() && !printsConstexpr(func)) func->setConstexprKind(clang::ConstexprSpecKind::Unspecified);
    }
    ~ConstexprDropper() { m_func->setConstexprKind(m_saved); }

  private:
    clang::FunctionDecl* m_func;
    clang::ConstexprSpecKind m_saved;
};

// The declaration written in place of a definition that is moved to the end, eg. `int f(int x)`
// of `int f(int x) { ... }`, or of a template, `template <class T> T twice(T x)`, whose definition
// is then only declared. It keeps what the definition was written with: its default arguments,
// attributes and template head. Default arguments stay where they were written, as code that stays
// generic still needs them, eg. a SFINAE check `decltype(a.allocate(n))`.
void DeclBeforeDefVisitor::getFunctionDeclaration(clang::FunctionDecl* func, std::string& decl) {
    llvm::raw_string_ostream decl_out(decl);
    clang::PrintingPolicy inplace_policy = m_inplace_policy;
    // the printer writes the attributes of an out-of-line member of a class template before its
    // `template <...>` head, `__attribute__((x)) template <...> ...`. This redeclaration doesn't
    // need them: the declaration in the class and the deferred definitions keep them
    if (!func->getDescribedFunctionTemplate() && func->getNumTemplateParameterLists() > 0)
        inplace_policy.PolishForDeclaration = true;
    // a deduced return type is printed as the type it was deduced to, canonically, so that
    // it can be named outside the function
    if (!func->isTemplated() && func->getReturnType()->getContainedDeducedType())
        inplace_policy.PrintAsCanonical = true;
    InheritedDefaultArgsHider hider(func);
    ConversionNameSpeller speller(func, inplace_policy);
    if (auto* function_template = func->getDescribedFunctionTemplate())
        function_template->print(decl_out, inplace_policy);
    else
        func->print(decl_out, inplace_policy);
    // an out-of-line member keeps its declaration, `int S::f() const;`, which can't repeat `static`
    if (func->isCXXClassMember() && func->isOutOfLine()) stripRecordDeclOnlySpecifiers(decl);
}

// Moves @p begin back over the `template <...>` heads before it, which the range of an out-of-line
// member template of a class template leaves out, eg. from `void hey<T>::foo()` back to the first
// `template <typename T>` of `template <typename T> template <typename U> void hey<T>::foo()`.
clang::SourceLocation DeclBeforeDefVisitor::expandToTemplateHeaders(clang::SourceLocation begin) {
    clang::SourceManager& source_mgr = m_ctx.getSourceManager();
    const char* buffer_start = source_mgr.getCharacterData(source_mgr.getLocForStartOfFile(source_mgr.getFileID(begin)));
    const char* cursor = source_mgr.getCharacterData(begin);
    auto skip_space = [buffer_start](const char* at) {
        while (at > buffer_start && isspace((unsigned char)at[-1])) --at;
        return at;
    };
    for (;;) {
        const char* scan = skip_space(cursor);
        if (scan == buffer_start || scan[-1] != '>') break;
        int angle_depth = 0;
        while (scan > buffer_start) {
            --scan;
            if (*scan == '>') ++angle_depth;
            else if (*scan == '<' && --angle_depth == 0) break;
        }
        if (*scan != '<') break;
        scan = skip_space(scan);
        if (scan - buffer_start < 8 || strncmp(scan - 8, "template", 8) != 0) break;
        cursor = scan - 8;
    }
    return begin.getLocWithOffset(cursor - source_mgr.getCharacterData(begin));
}

// clang reports a truncated end location for a function declaration whose trailing
// return type is a `decltype(...)`: the range ends at the function name. For
// declarations without a body, walk forward to the terminating `;` so that the whole
// declaration is replaced. For correct ranges this is a no-op. Brackets are tracked
// as a `decltype` may contain a lambda with its own `;`.
clang::SourceLocation DeclBeforeDefVisitor::expandToTerminatingSemi(clang::SourceLocation end) {
    const clang::SourceManager& source_mgr = m_ctx.getSourceManager();
    const clang::LangOptions& lang_opts = m_ctx.getLangOpts();
    // the range of a definition ends at the closing brace of its body.
    if (*source_mgr.getCharacterData(end) == '}') return end;
    clang::SourceLocation cursor = end;
    int depth = 0;
    for (;;) {
        std::optional<clang::Token> next = clang::Lexer::findNextToken(cursor, source_mgr, lang_opts);
        if (!next || next->is(clang::tok::eof)) return end;
        if (depth == 0 && next->is(clang::tok::semi)) return cursor;
        // a body at the top level means this is not a bare declaration.
        if (depth == 0 && next->is(clang::tok::l_brace)) return end;
        if (next->isOneOf(clang::tok::l_paren, clang::tok::l_square, clang::tok::l_brace)) ++depth;
        else if (next->isOneOf(clang::tok::r_paren, clang::tok::r_square, clang::tok::r_brace)) --depth;
        cursor = next->getLocation();
    }
}

// Writes the declaration @p decl of @p func where @p func is written, replacing its definition,
// eg. `int f(int x) { return x; }` becomes `int f(int x);`, and tells the driver's retries what
// it wrote there: the first line of a replaced definition (replaced()) or of a template reduced to
// a declaration (stripped()), so that an error on that line can be blamed on it.
void DeclBeforeDefVisitor::replaceFunctionDeclaration(clang::FunctionDecl* func, std::string& decl) {
    respellDecayedVaList(decl);
    hoistTrailingAttributes(decl);
    if (!func->isDependentContext() && !llvm::isa<clang::CXXDeductionGuideDecl>(func))
        m_replaced.push_back({m_names.getName(func), decl.substr(0, decl.find('\n'))});
    else if (func->doesThisDeclarationHaveABody())
        m_stripped.push_back({passKey(func), decl.substr(0, decl.find('\n'))});
    clang::SourceRange range = func->getSourceRange();
    if (auto* function_template = func->getDescribedFunctionTemplate()) {
        // the range of an out-of-line member template of a class template starts at its own
        // `template <class U>`, after the one of its class, eg. in
        //
        //     template <class T> template <class U> void hey<T>::foo() { ... }
        range = function_template->getSourceRange();
        if (func->isCXXClassMember() && func->isOutOfLine()) range.setBegin(expandToTemplateHeaders(range.getBegin()));
        // an abbreviated function template, eg. `void f(auto x)`, has no template head
        if (range.getBegin().isInvalid()) range.setBegin(func->getSourceRange().getBegin());
    }
    if (!func->doesThisDeclarationHaveABody()) range.setEnd(expandToTerminatingSemi(range.getEnd()));
    m_rewriter.ReplaceText(range, decl + ";");
}

// Every function definition, of the input and of every template instantiation (see
// shouldVisitTemplateInstantiations), goes through here once:
//  - what stays in place (isKeptByDeclBeforeDef) is left as written;
//  - an instantiation that gets a copy is only written as the copy, see monomorphize;
//  - a declaration without a body is written again, with the parameters renamed;
//  - a definition is replaced by its declaration. Unless it is a template, it is also recorded
//    for the end of the TU. An instantiation without a copy, eg. a constructor, is written there
//    as an explicit specialization:
//
//        template <class T> struct Box { Box(T v) {...} };   =>  template <class T> struct Box { Box(T v); };
//        ... at the end of the TU:                               template<> Box<int>::Box(int v){...}
//
// Class templates aren't specialized, only the functions: a class is only a type.
bool DeclBeforeDefVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    // a copy is written at the end where the definition it copies is, eg. that of `twice<int>`
    // where `twice` is defined, so that what is written at the end keeps the order of the TU
    if (auto copies = m_copies_at.find(func); copies != m_copies_at.end())
        for (size_t i : copies->second) m_written.push_back({true, i});
    if (isKeptByDeclBeforeDef(func, m_kept_in_place)) return true;
    // an instantiation that isn't specialized is only written as its copy
    if (m_mono && isInstantiatedCopy(func) && !isAlwaysSpecialized(func) && !m_specialized.count(func->getCanonicalDecl()))
        return true;
    // a declaration in a function, eg. a local `void g();`, is printed with the function's body
    if (isInsideFunction(func)) return true;
    if (!func->hasBody() || !func->isThisDeclarationADefinition()) {
        // a declaration is written again, with its renamed parameters. An instantiation, eg.
        // `foo<int>` of `template <class T> void foo();`, shares the template's text and isn't.
        if (!isInstantiatedCopy(func)) {
            std::string decl;
            getFunctionDeclaration(func, decl);
            replaceFunctionDeclaration(func, decl);
        }
        return true;
    }

    // clang renames the parameters of a specialization to those of its template when a
    // call uses one of its default arguments (Sema::addInstantiatedParametersToScope):
    //     template <class S> int spin(S s_in_template = S{});
    //     template <> int spin<int>(int s_in_spec) { return s_in_spec; }
    //     int use() { return spin<int>(); }
    // leaves `spin<int>` with a parameter named `s_in_template`, and its body uses that name.
    // Each pass parses the output of the previous one. A later pass printing part of the body,
    // eg. a loop condition, would name a parameter the parameter list doesn't declare. So the
    // parameters are named like the template's from the start, from its first declaration, as
    // its definition is deferred. A parameter pack names several parameters, so it is left alone.
    const clang::FunctionDecl* pattern = func->getTemplateInstantiationPattern(false);
    if (pattern) pattern = pattern->getFirstDecl();
    if (pattern && pattern != func && !func->isDependentContext() && pattern->getNumParams() == func->getNumParams() &&
        llvm::none_of(pattern->parameters(), [](const clang::ParmVarDecl* param) { return param->isParameterPack(); })) {
        for (unsigned i = 0; i < func->getNumParams(); ++i)
            func->getParamDecl(i)->setDeclName(pattern->getParamDecl(i)->getDeclName());
    }

    ConstexprDropper constexpr_dropper(func);
    std::string decl;
    // the heads of the definition and of the declaration written at the end, see recordFunctionDefinition
    std::string definition_head;
    std::string declaration_head;

    // a K&R C definition has no prototype to print
    if (!func->hasWrittenPrototype() && func->getNumParams() > 0) {
        getKRFunctionDeclarations(func, decl, definition_head);
        declaration_head = definition_head;
    } else {
        if (!isInstantiatedCopy(func)) getFunctionDeclaration(func, decl);

        // the heads, canonical and fully qualified, eg.
        // `template<> unsigned long std::vector<int>::size() const`, then fixed up below
        if (!func->isDependentContext()) {
            ConversionNameSpeller speller(func, m_deferred_policy_decl);
            llvm::raw_string_ostream head_out(definition_head);
            func->print(head_out, m_deferred_policy_decl);

            // a specialization is already constrained by its template and can't
            // repeat the trailing requires clause.
            if (const clang::Expr* constraint = func->getTrailingRequiresClause().ConstraintExpr) {
                clang::PrintingPolicy policy = m_deferred_policy_decl;
                policy.SuppressSpecifiers = false;
                std::string clause = " requires ";
                llvm::raw_string_ostream clause_out(clause);
                constraint->printPretty(clause_out, nullptr, policy, 0, "\n", &m_ctx);
                if (size_t pos = definition_head.rfind(clause); pos != std::string::npos)
                    definition_head.erase(pos, clause.size());
            }
            hoistTrailingAttributes(definition_head);
            declaration_head = definition_head;

            // the `noexcept(expr)` of a specialization is spelled by its value, the
            // expression may call what is only defined later, eg. with a deduced return type:
            // `noexcept(noexcept(swap(a, b)))` of `T = int` becomes `noexcept`
            const auto* proto = func->getType()->getAs<clang::FunctionProtoType>();
            if (func->isTemplateInstantiation() && proto && clang::isComputedNoexcept(proto->getExceptionSpecType()) &&
                proto->getNoexceptExpr()) {
                std::string spec = " noexcept(";
                llvm::raw_string_ostream spec_out(spec);
                proto->getNoexceptExpr()->printPretty(spec_out, nullptr, m_deferred_policy_decl, 0, "\n", &m_ctx);
                spec_out << ")";
                auto spell_noexcept = [&](std::string& head, const std::string& value) {
                    if (size_t pos = head.rfind(spec); pos != std::string::npos) head.replace(pos, spec.size(), value);
                };
                std::string value = proto->isNothrow() ? " noexcept" : " noexcept(false)";
                spell_noexcept(definition_head, value);
                // clang only matches the declaration of the specialization of a member of a class template
                // to the member once it worked out its exception specification, see printMemberProbe
                if (func->getInstantiatedFromMemberFunction())
                    value = std::string(" noexcept(") + (proto->isNothrow() ? "true" : "false") + " && sizeof(" +
                            printMemberProbe(m_ctx, func, m_deferred_policy_decl) + "))";
                spell_noexcept(declaration_head, value);
            }

            // the printer prints a deduced return type (eg. `auto`, `decltype(auto)` or
            // `Small auto`) as the type it was deduced to, but a specialization must repeat the
            // placeholder of its template, eg. `template<> auto get(int)`, so it is put back.
            // A function that isn't a template keeps the deduced type, as all its declarations do
            auto* auto_type = func->getReturnType()->getContainedAutoType();
            const clang::FunctionDecl* pattern = func->getTemplateInstantiationPattern();
            if (func->isTemplateInstantiation() && auto_type && auto_type->isDeduced() && pattern) {
                std::string deduced = func->getReturnType().getAsString(m_deferred_policy_decl);
                std::string placeholder = pattern->getReturnType().getAsString(m_deferred_policy_decl);
                auto ident = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
                for (std::string* head : {&definition_head, &declaration_head}) {
                    // the return type is outside of the qualifier's template arguments and the parameters
                    size_t pos = std::string::npos;
                    int depth = 0;
                    for (size_t i = 0; i < head->size() && pos == std::string::npos; ++i) {
                        char c = (*head)[i];
                        if (c == '<' || c == '(') ++depth;
                        else if ((c == '>' && (*head)[i - 1] != '-') || c == ')') --depth;
                        else if (depth == 0 && head->compare(i, deduced.size(), deduced) == 0 &&
                                 (i == 0 || (!ident((*head)[i - 1]) && (*head)[i - 1] != ':')) &&
                                 (i + deduced.size() == head->size() || !ident(deduced.back()) ||
                                  (!ident((*head)[i + deduced.size()]) && (*head)[i + deduced.size()] != ':')))
                            pos = i;
                    }
                    if (pos == std::string::npos) continue;
                    head->replace(pos, deduced.size(), placeholder);
                    // `int &get()` has no space after the deduced type, `decltype(auto) get()` needs one.
                    size_t end = pos + placeholder.size();
                    if (end < head->size() && ident((*head)[end]) && (ident(placeholder.back()) || placeholder.back() == ')'))
                        head->insert(end, " ");
                }
            }

            // the template arguments of a specialization are substituted into every template of
            // its name, which fails hard for some, eg. libstdc++'s `__niter_base(reverse_iterator<It>)`
            // for an ostream_iterator, or the `operator+` of two durations given those of a time_point
            // and a duration. They are deduced from the parameters instead when they can be, which
            // compares types without instantiating any.
            if (const clang::FunctionTemplateDecl* primary = func->getPrimaryTemplate();
                primary && !llvm::isa<clang::CXXConstructorDecl, clang::CXXConversionDecl>(func)) {
                llvm::SmallBitVector deducible;
                clang::Sema::MarkDeducedTemplateParameters(m_ctx, primary, deducible);
                if (deducible.all() && countDeducedTemplates(m_sema, func, false) == 1) {
                    std::string name = func->isOverloadedOperator()
                                           ? std::string("operator") + clang::getOperatorSpelling(func->getOverloadedOperator())
                                           : func->getNameAsString();
                    stripTemplateArguments(definition_head, name);
                    stripTemplateArguments(declaration_head, name);
                }
            }
        }
    }

    // a template has no code of its own. Anything else is defined at the end.
    if (!func->isDependentContext()) recordFunctionDefinition(func, definition_head, declaration_head);

    // the definition where it is written becomes its declaration. The instantiations of a template
    // share its text, which is replaced once, through the template itself.
    if (!isInstantiatedCopy(func)) replaceFunctionDeclaration(func, decl);
    return true;
}

// The specializations whose return type is deduced, as indices in the deferred definitions, each
// after the ones it calls. Such a specialization keeps the `auto` of its template, so it can't be
// called before it is defined, and these are written first. eg. with `template <class T> auto get(T t)`,
// `template<> auto get(int t){...}` is written before `int use() { return get(1) + 1; }`.
std::vector<size_t> DeclBeforeDefVisitor::deferredDefinitionOrder() const {
    auto deduced = [](const clang::FunctionDecl* func) {
        const auto* auto_type = func->getReturnType()->getContainedAutoType();
        return func->isTemplateInstantiation() && auto_type && auto_type->isDeduced();
    };
    std::map<const clang::FunctionDecl*, size_t> deduced_index;
    for (size_t i = 0; i < m_deferred_functions.size(); ++i)
        if (deduced(m_deferred_functions[i])) deduced_index[m_deferred_functions[i]->getCanonicalDecl()] = i;

    std::vector<size_t> order;
    std::vector<bool> placed(m_deferred_functions.size(), false);
    std::function<void(size_t)> place = [&](size_t i) {
        if (placed[i]) return;
        placed[i] = true;
        std::function<void(const clang::Stmt*)> callees = [&](const clang::Stmt* stmt) {
            if (!stmt) return;
            const clang::ValueDecl* used = nullptr;
            if (const auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt)) used = ref->getDecl();
            if (const auto* member = llvm::dyn_cast<clang::MemberExpr>(stmt)) used = member->getMemberDecl();
            if (const auto* callee = llvm::dyn_cast_or_null<clang::FunctionDecl>(used))
                if (auto it = deduced_index.find(callee->getCanonicalDecl()); it != deduced_index.end()) place(it->second);
            for (const clang::Stmt* child : stmt->children()) callees(child);
        };
        callees(m_deferred_functions[i]->getBody());
        order.push_back(i);
    };
    for (size_t i = 0; i < m_deferred_functions.size(); ++i)
        if (deduced(m_deferred_functions[i])) place(i);
    return order;
}

// What is written at the end of the TU, in this order:
//  1. the helper `__mono_addr` that takes the address of a temporary object, when a copy is called;
//  2. the declaration of every deferred definition;
//  3. the declaration of every copy;
//  4. the deferred definitions and the definitions of the copies, in the order of the TU. A copy
//     goes where its function is written. What a definition needs defined before it, eg. a
//     function a `constexpr` variable's initializer calls, was before it in the input too. The
//     specializations whose return type is deduced come first, see deferredDefinitionOrder.
// Declaring everything first lets any definition call any function. It also keeps a
// specialization from being declared after a definition instantiated it, eg. `template<> int
// bar<int>()` after `int zed() { return foo<int>(); }` where `foo<T>` calls `bar<T>`. The lines
// of each entry are recorded, see entries(), so the driver can blame an error on its function.
std::string DeclBeforeDefVisitor::getDeferredDefinitions() {
    // the lines of what is added, counted from 1 at the first line of the result
    std::string result = "\n";
    size_t lines = 1;
    auto add = [&](const std::string& text, const std::string& key, bool copy) {
        size_t first = lines + 1;
        result += text;
        result += "\n";
        lines += std::count(text.begin(), text.end(), '\n') + 1;
        m_entries.push_back({key, first, lines, copy});
    };

    // a copy's object, a temporary, has its address taken while it is alive
    if (m_mono && !m_copied.empty())
        add("template <class T> T *__mono_addr(T &&x) { return __builtin_addressof(x); }", "", false);
    for (size_t i = 0; i < m_deferred_declarations.size(); ++i)
        add(m_deferred_declarations[i], m_deferred_keys[i], false);

    // the copies are declared before anything deferred calls them
    std::vector<std::string> headers;
    for (clang::FunctionDecl* func : m_copied) {
        headers.push_back(copyHeader(func, m_copies.at(func->getCanonicalDecl())));
        add(headers.back() + ";", m_names.getName(func), true);
    }

    result += "\n";
    ++lines;
    // the copies, eg. `template <class T> struct Box { T v; T get() const { return v; } }` gives
    // `get` of `Box<int>` the copy `[[gnu::used]] inline int get__mono...(const Box<int> *__this){
    // return __this->v; }`
    auto add_copy = [&](size_t i) {
        clang::FunctionDecl* func = m_copied[i];
        clang::Stmt* body = func->getBody();
        // `this` is the first parameter, and the names the body uses are looked up from the global namespace
        if (auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(func); method && method->isInstance()) {
            auto* this_var = clang::VarDecl::Create(m_ctx, method, {}, {}, &m_ctx.Idents.get("__this"), method->getThisType(),
                                                    nullptr, clang::SC_None);
            replaceThis(body, clang::DeclRefExpr::Create(m_ctx, {}, {}, this_var, false, clang::SourceLocation(),
                                                         this_var->getType(), clang::VK_PRValue));
        }
        qualifyNonLocalNames(body, m_ctx);
        redirectCalls(body);
        MonoPrinterHelper printer(m_ctx, m_copy_policy, m_copies);
        std::string def;
        llvm::raw_string_ostream def_out(def);
        body->printPretty(def_out, &printer, m_copy_policy);
        add("[[gnu::used]] inline " + headers[i] + def_out.str(), m_names.getName(func), true);
    };
    std::vector<bool> deferred_done(m_deferred_definitions.size(), false);
    std::vector<bool> copy_done(m_copied.size(), false);
    for (size_t i : deferredDefinitionOrder()) {
        deferred_done[i] = true;
        add(m_deferred_definitions[i], m_deferred_keys[i], false);
    }
    for (auto [copy, i] : m_written) {
        if (copy ? copy_done[i] : deferred_done[i]) continue;
        (copy ? copy_done : deferred_done)[i] = true;
        if (copy) add_copy(i);
        else add(m_deferred_definitions[i], m_deferred_keys[i], false);
    }
    // a copy of a function the traversal didn't reach still has to be defined
    for (size_t i = 0; i < m_copied.size(); ++i)
        if (!copy_done[i]) add_copy(i);

    // `__builtin_va_list` decayed in a parameter prints as `__va_list_tag *`, which isn't a name
    respellDecayedVaList(result);
    return result;
}

// --- monomorphize --------------------------------------------------

namespace {

// Whether @p stmt declares a static local that isn't constant, eg. `static int n; return ++n;`.
// A copy would get its own `n`, and count apart from the original that code left in place still calls.
bool hasStaticLocal(const clang::Stmt* stmt) {
    if (!stmt) return false;
    if (const auto* decls = llvm::dyn_cast<clang::DeclStmt>(stmt))
        for (const clang::Decl* decl : decls->decls())
            if (const auto* var = llvm::dyn_cast<clang::VarDecl>(decl);
                var && var->isStaticLocal() && !var->isConstexpr() && !var->getType().isConstQualified())
                return true;
    return llvm::any_of(stmt->children(), hasStaticLocal);
}

}  // namespace

// eg. `Box<int>::get()` is copyable; `main`, a constructor, a lambda's call operator, or a function
// taking a type local to another function, eg. `void f(Local)`, isn't, as the copy is a free
// function at the end of the TU that must be named and called like any other.
bool isCopyable(const clang::FunctionDecl* func, bool specialized) {
    if (!func->doesThisDeclarationHaveABody() || !func->getBody() || func->isDependentContext() || func->isMain() ||
        func->isConsteval() || func->isDefaulted() || func->isDeleted() || isInsideFunction(func) ||
        llvm::isa<clang::CXXDeductionGuideDecl>(func) ||
        (!specialized && (llvm::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl>(func) || hasStaticLocal(func->getBody()))))
        return false;
    const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(func);
    if (method && (method->getParent()->isLambda() ||
                   !isNameableAnywhere(func->getASTContext().getCanonicalTagType(method->getParent()))))
        return false;
    return isNameableAnywhere(func->getReturnType()) &&
           llvm::none_of(func->parameters(), [](const clang::ParmVarDecl* param) { return !isNameableAnywhere(param->getType()); });
}

bool isAlwaysSpecialized(const clang::FunctionDecl* func) {
    const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(func);
    return llvm::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl>(func) || (method && method->isVirtual());
}

namespace {

// The functions given a copy, see DeclBeforeDefVisitor::monomorphize: the copyable template
// instantiations, `constexpr` functions, which stay in place for the compiler to evaluate, and
// what DeclBeforeDef leaves as written. A virtual function isn't copied: its specialization is
// written for the vtable anyway, and its calls call that.
class CopiedCollector : public clang::RecursiveASTVisitor<CopiedCollector> {
  public:
    CopiedCollector(const std::set<const clang::FunctionDecl*>& kept) : m_kept(kept) {}
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool VisitFunctionDecl(clang::FunctionDecl* func) {
        if (isCopyable(func) && !isAlwaysSpecialized(func) &&
            (isInstantiatedCopy(func) || func->isConstexpr() || isKeptByDeclBeforeDef(func, m_kept)))
            m_found.push_back(func);
        return true;
    }
    std::vector<clang::FunctionDecl*> m_found;

  private:
    const std::set<const clang::FunctionDecl*>& m_kept;
};

}  // namespace

// Picks the functions that get a copy, see CopiedCollector, and names the copies. A copy is a
// free function at the end that takes the object of a member function first. What is deferred
// and the copies call the copies, so that each function they call has a body at the end to inline.
void DeclBeforeDefVisitor::monomorphize() {
    CopiedCollector collector(m_kept_in_place);
    collector.TraverseDecl(m_ctx.getTranslationUnitDecl());
    for (clang::FunctionDecl* func : collector.m_found) {
        std::string key = m_names.getName(func);
        if (m_dropped.count(key) || m_copies.count(func->getCanonicalDecl())) continue;
        std::string base = func->getIdentifier() ? func->getName().str()
                           : llvm::isa<clang::CXXConversionDecl>(func) ? "conversion"
                                                                        : "operator";
        std::string name = copyName(base, key);
        m_copies[func->getCanonicalDecl()] = name;
        const clang::FunctionDecl* pattern = func->getTemplateInstantiationPattern();
        m_copies_at[pattern ? pattern : func->getDefinition()].push_back(m_copied.size());
        m_copied.push_back(func);
        m_copy_keys[name] = key;
    }
}

// A function only printed, as the callee of a call: a declaration of no type with the name of a
// copy, eg. `size__mono1a2b3c4d5e`, or of `__builtin_addressof`, which redirectCalls refers to.
// The AST it is put in is only printed, never checked, so its type doesn't matter.
clang::FunctionDecl* DeclBeforeDefVisitor::printedFunction(const std::string& name) {
    clang::FunctionDecl*& func = m_printed[name];
    if (!func)
        func = clang::FunctionDecl::Create(m_ctx, m_ctx.getTranslationUnitDecl(), {}, {}, clang::DeclarationName(&m_ctx.Idents.get(name)),
                                           m_ctx.getFunctionType(m_ctx.VoidTy, {}, {}), nullptr, clang::SC_None);
    return func;
}

// Has the calls in @p node of what is given a copy call the copy, in the AST, as a call
// in a declaration isn't printed with a PrinterHelper. A copy takes the object of a
// member function first, eg.
//
//     v.push_back(x)            =>  push_back__mono...(__builtin_addressof(v), x)
//     it->second.size()         =>  size__mono...(__builtin_addressof(it->second))
//     make().get()              =>  get__mono...(__mono_addr(make()))
//     auto f = &twice<int>;     =>  auto f = &twice__mono...;
void DeclBeforeDefVisitor::redirectCalls(clang::Stmt*& node) {
    // a constant expression is evaluated by the compiler, which needs the definition of what it
    // calls before it
    if (!node || llvm::isa<clang::ConstantExpr>(node)) return;
    // a comparison rewritten from another calls the copy as what it was rewritten to, eg. `a != b`
    // => `!operator__mono...(a, b)`
    if (auto* rewritten = llvm::dyn_cast<clang::CXXRewrittenBinaryOperator>(node)) {
        clang::Expr* form = rewrittenForm(m_ctx, rewritten, m_copies);
        if (!form) return;
        node = form;
    }
    // the address of a function, rather than a call of it, is that of its copy, which has its type
    if (auto* call = llvm::dyn_cast<clang::CallExpr>(node)) m_callees.insert(call->getCallee()->IgnoreParenImpCasts());
    if (auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(node); ref && !m_callees.count(ref))
        if (auto* func = llvm::dyn_cast<clang::FunctionDecl>(ref->getDecl());
            func && !(llvm::isa<clang::CXXMethodDecl>(func) && llvm::cast<clang::CXXMethodDecl>(func)->isInstance()) &&
            m_copies.count(func->getCanonicalDecl())) {
            node = clang::DeclRefExpr::Create(m_ctx, {}, {}, printedFunction(m_copies.at(func->getCanonicalDecl())), false,
                                              ref->getLocation(), ref->getType(), ref->getValueKind());
            return;
        }
    // the arguments and the object first, so that the call built below holds redirected ones
    for (clang::Stmt*& child : node->children()) redirectCalls(child);
    // and so are those of the functions of a local class, eg. the destructor of a guard
    if (auto* decls = llvm::dyn_cast<clang::DeclStmt>(node))
        for (clang::Decl* decl : decls->decls())
            if (auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
                for (clang::CXXMethodDecl* method : record->methods()) {
                    if (clang::Stmt* body = method->getBody()) redirectCalls(body);
                    if (auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(method))
                        for (clang::CXXCtorInitializer* init : ctor->inits())
                            if (clang::Stmt* value = init->getInit()) redirectCalls(value);
                }
    // braces print their syntactic form, a separate list of the same elements
    if (auto* list = llvm::dyn_cast<clang::InitListExpr>(node); list && list->getSyntacticForm())
        for (clang::Stmt*& child : list->getSyntacticForm()->children()) redirectCalls(child);
    auto* call = llvm::dyn_cast<clang::CallExpr>(node);
    const clang::FunctionDecl* callee = call ? call->getDirectCallee() : nullptr;
    auto found = callee ? m_copies.find(callee->getCanonicalDecl()) : m_copies.end();
    if (found == m_copies.end()) return;

    // the object: the first argument of a member operator, eg. `a` of `a + b`, or the base of
    // the member expression, eg. `v` of `v.size()`
    const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
    clang::Expr* object = nullptr;
    bool arrow = false;
    unsigned first = 0;
    if (llvm::isa<clang::CXXOperatorCallExpr>(call) && method) {
        if (method->isInstance()) object = call->getArg(0);
        first = 1;
    } else if (method && method->isInstance()) {
        auto* member = llvm::dyn_cast<clang::MemberExpr>(call->getCallee()->IgnoreParens());
        if (!member) return;
        object = member->getBase();
        arrow = member->isArrow();
    }
    auto call_of = [&](const std::string& name, llvm::ArrayRef<clang::Expr*> args, clang::QualType type, clang::ExprValueKind kind) {
        clang::FunctionDecl* func = printedFunction(name);
        auto* ref = clang::DeclRefExpr::Create(m_ctx, {}, {}, func, false, clang::SourceLocation(), func->getType(), clang::VK_LValue);
        return clang::CallExpr::Create(m_ctx, ref, args, type, kind, clang::SourceLocation(), clang::FPOptionsOverride());
    };
    std::vector<clang::Expr*> args;
    if (object)
        args.push_back(arrow ? object
                             : call_of(object->isLValue() && !object->IgnoreParenImpCasts()->isPRValue()
                                           ? "__builtin_addressof" : "__mono_addr", {object},
                                       m_ctx.getPointerType(object->getType()), clang::VK_PRValue));
    for (unsigned i = first; i < call->getNumArgs(); ++i) {
        clang::Expr* arg = call->getArg(i);
        // a default argument is written where the function is declared, so its names are qualified
        if (auto* default_arg = llvm::dyn_cast<clang::CXXDefaultArgExpr>(arg)) {
            clang::Stmt* value = default_arg->getExpr();
            qualifyNonLocalNames(value, m_ctx);
            redirectCalls(value);
            arg = llvm::cast<clang::Expr>(value);
        }
        args.push_back(arg);
    }
    node = call_of(found->second, args, call->getType(), call->getValueKind());
}

// The head of the copy @p name of @p func: a free function taking the object of a member first, eg.
// `int Box<int>::get() const noexcept` gives `int get__mono...(const Box<int> *__this) noexcept`. It is
// `static` when @p func has internal linkage, and `constexpr` when it can be a constant expression.
std::string DeclBeforeDefVisitor::copyHeader(clang::FunctionDecl* func, const std::string& name) {
    std::string params;
    llvm::raw_string_ostream params_out(params);
    const char* separator = "";
    if (auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(func); method && method->isInstance()) {
        method->getThisType().print(params_out, m_copy_policy, "__this");
        separator = ", ";
    }
    // the parameters of an expanded pack share their name, eg. `Ts... ts` of `<int, int>` gives
    // `int ts, int ts_1`
    std::set<std::string> seen;
    for (clang::ParmVarDecl* param : func->parameters()) {
        std::string param_name = param->getName().str();
        if (!param_name.empty() && !seen.insert(param_name).second) {
            param_name += "_" + std::to_string(param->getFunctionScopeIndex());
            param->setDeclName(&m_ctx.Idents.get(param_name));
        }
        params_out << separator;
        param->getType().print(params_out, m_copy_policy, param_name);
        separator = ", ";
    }
    const auto* proto = func->getType()->getAs<clang::FunctionProtoType>();
    if (proto && proto->isVariadic()) params_out << separator << "...";
    // the copies of a function of internal linkage in other TUs are other functions
    std::string header = func->isExternallyVisible() ? "" : "static ";
    if (printsConstexpr(func)) header += "constexpr ";
    llvm::raw_string_ostream header_out(header);
    // `noexcept` is part of the declarator, eg. `const char (*f() noexcept)[3]`
    bool nothrow = proto && !clang::isUnresolvedExceptionSpec(proto->getExceptionSpecType()) && proto->isNothrow();
    func->getReturnType().print(header_out, m_copy_policy, name + "(" + params_out.str() + ")" + (nothrow ? " noexcept" : ""));
    return header_out.str();
}

}  // namespace invisible
