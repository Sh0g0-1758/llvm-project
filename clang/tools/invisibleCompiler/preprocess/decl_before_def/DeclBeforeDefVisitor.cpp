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

#include <string>
#include <queue>
#include <stack>

namespace invisible {

namespace {

/// The printer puts a `[[...]]` written after the name of a function, eg.
/// `operator() [[nodiscard]] (int)`, after its type, where it would apply to the
/// type. In front of the declaration it applies to the function. It is dropped
/// when a requires clause of the template head is in the way, these being eg.
/// `nodiscard` or `deprecated`, which only matter for diagnostics.
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
        int depth = 0;
        size_t close = open;
        for (; close < decl.size(); ++close) {
            if (decl[close] == '<' || decl[close] == '(') ++depth;
            if ((decl[close] == '>' || decl[close] == ')') && --depth == 0) break;
        }
        at = decl.find_first_not_of(" \n", close + 1);
        if (at == std::string::npos) return;
    }
    if (decl.compare(at, 8, "requires") != 0) decl.insert(at, attributes + " ");
}

}  // namespace

// ─── DeclBeforeDefVisitor ──────────────────────────────────────────────

bool DeclBeforeDefVisitor::shouldVisitTemplateInstantiations() const { return true; }

DeclBeforeDefVisitor::DeclBeforeDefVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_deferred_policy_decl(m_ctx.getLangOpts()), m_deferred_policy_ctor(m_ctx.getLangOpts()), m_deferred_policy_decl_before_def(m_ctx.getLangOpts()), m_inplace_policy(m_ctx.getLangOpts()), m_custom_printer(ctx, m_policy) {
    if (m_ctx.getLangOpts().CPlusPlus) {
        m_policy.adjustForCPlusPlus();
    }
    m_policy.IncludeTagDefinition = true;


    m_deferred_policy_ctor = m_policy;
    m_deferred_policy_ctor.PrintAsCanonical = true;
    m_deferred_policy_ctor.FullyQualifiedName = true;


    m_deferred_policy_decl = m_policy;
    // certain types defined within a record are not printed properly ie.
    // when the types are referenced, they are not properly qualified. 
    // By printing their canonicalized form, we circumvent this problem. 
    m_deferred_policy_decl.PrintAsCanonical = true;
    m_deferred_policy_decl.FullyQualifiedName = true;
    m_deferred_policy_decl.TerseOutput = true;
    // default arguments are not printed for any function declaration.
    // instead, the default arguments are materialized at the callSite itself. 
    m_deferred_policy_decl.SuppressInitializers = true;


    m_deferred_policy_decl_before_def = m_deferred_policy_decl;
    m_deferred_policy_decl_before_def.SuppressInitializers = true;


    m_inplace_policy = m_policy;
    m_inplace_policy.TerseOutput = true;
    m_inplace_policy.SuppressTemplateArgsInCXXConstructors = true;
    m_inplace_policy.SuppressInitializers = true;
}

void DeclBeforeDefVisitor::fixParameterPack(std::string& str, clang::FunctionDecl* func) {
    const clang::TemplateArgumentList* args = func->getTemplateSpecializationArgs();
    if (!args) return;

    std::string correct;
    {
        llvm::raw_string_ostream correct_out(correct);
        clang::printTemplateArgumentList(correct_out, args->asArray(), m_deferred_policy_decl);
        correct_out.flush();
    }

    const std::string name = func->getNameAsString();
    const size_t name_pos = str.find(name + "<");
    if (name_pos == std::string::npos) return;

    const size_t open = name_pos + name.size();

    size_t depth = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < str.size(); ++i) {
        if (str[i] == '<') {
            ++depth;
        } else if (str[i] == '>' && --depth == 0) {
            close = i;
            break;
        }
    }
    if (close == std::string::npos) return;

    str.replace(open, close + 1 - open, correct);
}

// removes the template argument list following the constructor's name, ie. the
// `<...>` in `::Name<...>(`. The class's own template arguments in the qualifier
// (`Name<...>::`) are followed by `::` instead and are left untouched.
void DeclBeforeDefVisitor::stripConstructorTemplateArgs(std::string& str, clang::CXXConstructorDecl* ctor) {
    const std::string name = "::" + ctor->getParent()->getNameAsString() + "<";
    for (size_t pos = str.find(name); pos != std::string::npos; pos = str.find(name, pos + 1)) {
        const size_t open = pos + name.size() - 1;
        size_t depth = 0;
        size_t close = std::string::npos;
        for (size_t i = open; i < str.size(); ++i) {
            if (str[i] == '<') {
                ++depth;
            } else if (str[i] == '>' && --depth == 0) {
                close = i;
                break;
            }
        }
        if (close != std::string::npos && close + 1 < str.size() && str[close + 1] == '(') {
            str.erase(open, close + 1 - open);
            return;
        }
    }
}

// removes the exception specification following a destructor's empty parameter
// list, ie. the ` noexcept` or ` noexcept(...)` in `~Name() noexcept`.
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

unsigned DeclBeforeDefVisitor::countEnclosingTemplateLevels(const clang::FunctionDecl* function_decl) {
    unsigned levels = 0;
    // an explicit specialization already emits the correct number of enclosing template<> specifiers. 
    if (function_decl->getTemplateSpecializationKind() == clang::TSK_ExplicitSpecialization) {
        return levels;
    }
    for (const clang::DeclContext* decl_context = function_decl->getDeclContext(); decl_context; decl_context = decl_context->getParent()) {
        if (const auto* record_decl = llvm::dyn_cast<clang::CXXRecordDecl>(decl_context)) {
            if (const auto* spec_decl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record_decl)) {
                // A class template specialization only needs the template<> keyword if
                // its not explicitly specialized in the TU. If it is, then its treated
                // as a normal class. 
                if (spec_decl->getSpecializationKind() != clang::TSK_ExplicitSpecialization) {
                    ++levels;
                }
            }
        }
    }
    return levels;
}

// strip specifiers that are legal only inside the class definition
void DeclBeforeDefVisitor::stripRecordDeclOnlySpecifiers(std::string& s) {
    auto isWordChar = [](char c) { return isalnum((unsigned char)c) || c == '_'; };

    for (llvm::StringRef kw : {"static ", "virtual ", "friend "}) {
        size_t pos = 0;
        while ((pos = s.find(kw.str(), pos)) != std::string::npos) {
            if (pos > 0 && isWordChar(s[pos - 1])) {
                ++pos;
                continue;
            }
            s.erase(pos, kw.size());
        }
    }

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

    for (llvm::StringRef kw : {" override", " final", " override"}) {
        if (llvm::StringRef(s).ends_with(kw)) {
            s.resize(s.size() - kw.size());
        }
    }
}

// recurse into the callExpr, visit all its children and reason
// if any of them is a default argument. If so, fully qualify the
// identifiers used by this expression. 
void DeclBeforeDefVisitor::materializeDefaultArgs(clang::Stmt* node) {
    if (!node) return;

    for (clang::Stmt*& child : node->children()) {
        if (!child) continue;

        if (auto* default_arg = llvm::dyn_cast<clang::CXXDefaultArgExpr>(child)) {
            child = default_arg->getExpr();
            qualifyNonLocalNames(child, m_ctx);
        }

        materializeDefaultArgs(child);
    }
}

std::string DeclBeforeDefVisitor::printCtorInitializers(clang::CXXConstructorDecl* ctor) {
    std::string result;
    llvm::raw_string_ostream out(result);
    bool has_initializer_list = false;

    for (const clang::CXXCtorInitializer* init : ctor->inits()) {
        if (init->isInClassMemberInitializer()) continue;
        if (!init->isWritten()) continue;

        if (!has_initializer_list) {
            out << " : ";
            has_initializer_list = true;
        } else {
            out << ", ";
        }

        if (init->isAnyMemberInitializer()) {
            out << init->getAnyMember()->getName();
        } else if (init->isDelegatingInitializer()) {
            out << ctor->getNameAsString();
        } else {
            out << clang::QualType(init->getBaseClass(), 0).getAsString(m_deferred_policy_ctor);
        }

        if (clang::Expr* expr = init->getInit()) {
            bool out_parens = !llvm::isa<clang::InitListExpr>(expr);
            if (out_parens) out << "(";

            if (auto* cleanups = llvm::dyn_cast<clang::ExprWithCleanups>(expr))
                expr = cleanups->getSubExpr();
            expr = expr->IgnoreParens();

            clang::Expr* simple_init = nullptr;
            clang::Expr** args = nullptr;
            unsigned num_args = 0;

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

    out.flush();
    return result;
}

void DeclBeforeDefVisitor::record_function_definition(clang::FunctionDecl* func, std::string& deferred_decl, std::string& deferred_decl_before_def) {
    // template instantiations that do not physically live in the current TU should not be deferred. 
    // Their definitions are still present in the AST. For eg. 
    //     template<typename T>
    //     class A {
    //     public:
    //         int foo();
    //     };
    //    
    //     int caller() {
    //         A<int> a;
    //         a.foo();
    //     }
    //    
    //     extern template class A<int>;
    // marking a class as extern also makes classes nested in it extern. This is basically 
    // telling the compiler to just assume that some function `foo` exists. Deferring such 
    // functions results in an unparsable TU because C++ forbids specialization after 
    // instantiation. Marking a class as extern is basically instantiating it. 
    if (func->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDeclaration) {
        return;
    }
    /**
     * By default, when printing a templatized function, libTooling
     * only prints one empty template<> brace. However, the function
     * can be nested in multiple levels of templated namespaces and
     * classes. So we manually calculate that depth and emit the 
     * corresponding number of template<> required. There is also additional
     * logic to fix variadic templates. The problem here is that parameter
     * packs are printed with enclosing braces <> which is syntatically wrong.
     * We print the correct set of template parameters and paste it at the 
     * correct place in the function deferred declaration. For the member-initializer
     * list present in constructors, its not printed as part of the declaration because
     * we pass the TerseOutput flag to be true. Its also not part of the function body
     * either. Thus we manually insert it when we encounter a constructor that needs one. 
     * There are also some Specifiers [static, virtual, friend, explicit, override, final]
     * that are only valid inside Record Declarations. The act of deferring them means that
     * we will have to manually strip the declarations of these keywords as Libtooling does not
     * support any flag for this. 
     */
    std::string def;
    // the deferred decl is already fully qualified, we do not
    // need the deferred def to be fully qualified as well. 
    // in fact, fully qualifying this can lead to errors, for 
    // instance re-declaration of functions in block scope. 
    llvm::raw_string_ostream def_out(def);
    materializeDefaultArgs(func->getBody());
    if (func->getFriendObjectKind() != clang::Decl::FOK_None) {
        // a friend is not a member: its deferred decl is not qualified by the class,
        // so its body is not in the class scope either. Print the types canonically
        // and qualify the names so that the class's typedefs, enumerators and static
        // members still resolve.
        clang::PrintingPolicy body_policy = m_policy;
        body_policy.PrintAsCanonical = true;
        clang::Stmt* body = func->getBody();
        qualifyNonLocalNames(body, m_ctx);
        body->printPretty(def_out, &m_custom_printer, body_policy);
    } else {
        func->getBody()->printPretty(def_out, &m_custom_printer, m_policy);
    }
    def_out.flush();

    std::string prefix{};
    if (unsigned extra = countEnclosingTemplateLevels(func)) {
        for (unsigned i = 0; i < extra; ++i) {
            prefix += "template<> ";
        }
    }

    std::string ctor_init{};
    if (auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(func)) {
        ctor_init = printCtorInitializers(ctor);
    }

    // constructor templates can't be given explicit template arguments, they are
    // always deduced from the parameters. Hence the specialization's template
    // arguments are dropped from its name, which also covers parameter packs.
    if (auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(func); ctor && ctor->getPrimaryTemplate()) {
        stripConstructorTemplateArgs(deferred_decl, ctor);
        stripConstructorTemplateArgs(deferred_decl_before_def, ctor);
    } else if (auto primary_template = func->getPrimaryTemplate()) {
        if (primary_template->getTemplateParameters()->hasParameterPack()) {
            fixParameterPack(deferred_decl, func);
            fixParameterPack(deferred_decl_before_def, func);
        }
    }

    // destructors are implicitly noexcept and the printer spells that out. A
    // specialization declared and defined with an explicit `noexcept` while the
    // destructor in the pattern has no written exception specification is
    // rejected, hence the printed one is dropped in that case.
    if (auto* dtor = llvm::dyn_cast<clang::CXXDestructorDecl>(func)) {
        const clang::FunctionDecl* pattern = dtor->getTemplateInstantiationPattern();
        if (pattern && pattern->getExceptionSpecSourceRange().isInvalid()) {
            stripDestructorExceptionSpec(deferred_decl);
            stripDestructorExceptionSpec(deferred_decl_before_def);
        }
    }

    if (m_ctx.getLangOpts().CPlusPlus && llvm::isa<clang::CXXMethodDecl>(func)) {
        stripRecordDeclOnlySpecifiers(deferred_decl);
        stripRecordDeclOnlySpecifiers(deferred_decl_before_def);
    }

    // clang rejects the definition of the specialization of a member of a class
    // template whose exception specification is `noexcept(expr)` once it has been
    // declared. It is only defined, ahead of the definitions that could use it.
    const clang::FunctionDecl* member = func->getInstantiatedFromMemberFunction();
    const auto* proto = member ? member->getType()->getAs<clang::FunctionProtoType>() : nullptr;
    if (proto && clang::isComputedNoexcept(proto->getExceptionSpecType())) {
        m_undeclared_definitions.push_back(prefix + deferred_decl + ctor_init + def);
        return;
    }

    m_deferred_definitions.push_back(prefix + deferred_decl + ctor_init + def);
    m_deferred_functions.push_back(func);
    m_deferred_declarations.push_back(prefix + deferred_decl_before_def + ";");
}

void DeclBeforeDefVisitor::get_kr_function_declarations(clang::FunctionDecl* func, std::string& decl, std::string& deferred_decl) {
    llvm::raw_string_ostream decl_out(decl);
    llvm::raw_string_ostream deferred_decl_out(deferred_decl);

    std::string proto = func->getNameAsString();
    proto += "(";
    for (unsigned i = 0; i < func->getNumParams(); ++i) {
        if (i > 0) proto += ", ";
        clang::ParmVarDecl* param_decl = func->getParamDecl(i);
        std::string param;
        llvm::raw_string_ostream param_out(param);
        param_decl->getType().print(param_out, m_inplace_policy, param_decl->getNameAsString());
        param_out.flush();
        proto += param;
    }
    proto += ")";

    func->getReturnType().print(decl_out, m_inplace_policy, proto);
    decl_out.flush();

    func->getReturnType().print(deferred_decl_out, m_deferred_policy_decl, proto);
    deferred_decl_out.flush();
}

clang::FriendDecl* DeclBeforeDefVisitor::get_friend_declaration(clang::FunctionDecl* func) {
    if (func->getFriendObjectKind() == clang::Decl::FOK_None) {
        return nullptr;
    }
    
    for (const clang::Decl* child = func;;) {
        auto parents = m_ctx.getParents(*child);
        if (parents.empty()) {
            return nullptr;
        }
        const auto* parent = parents[0].get<clang::Decl>();
        if(llvm::isa<clang::FriendDecl>(parent)) {
            return const_cast<clang::FriendDecl*>(llvm::dyn_cast<clang::FriendDecl>(parent));
        }
        child = parent;
    }
}

void DeclBeforeDefVisitor::fix_friend_declaration(std::string& decl, const clang::FunctionTemplateDecl* tmpl,
                                                  const clang::PrintingPolicy& policy) {
    size_t lt = decl.find('<');
    int depth = 0;
    size_t i = lt;
    for (; i < decl.size(); ++i) {
        if (decl[i] == '<') ++depth;
        else if (decl[i] == '>' && --depth == 0) break;
    }
    // along with the requires clause of the template head
    if (const clang::Expr* constraint = tmpl->getTemplateParameters()->getRequiresClause()) {
        std::string clause = " requires ";
        llvm::raw_string_ostream clause_out(clause);
        constraint->printPretty(clause_out, nullptr, policy, 0, "\n", &m_ctx);
        if (decl.compare(i + 1, clause.size(), clause) == 0) i += clause.size();
    }

    std::string header = decl.substr(strlen("friend "), i - strlen("friend ") + 1);
    std::string rest = decl.substr(i + 1);
    decl = header + " friend " + rest;
}

// Clang copies a default argument onto every redeclaration that inherits it and
// the printer would print it on all of them. Hides the inherited ones while the
// declaration is printed, so that each default only appears where it was written.
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

// clang prints the name of a conversion to a class as `operator` and the name of the
// class alone, eg. `operator basic_string_view`, which is not a type once the class
// is a template specialization. The conversion is named after its type while printed.
// The destructor of a member class template is named with the template arguments of
// both classes, eg. `~_Impl<A...>::_Impl<B...>`, and is named `~_Impl` instead.
class ConversionNameSpeller {
  public:
    ConversionNameSpeller(clang::FunctionDecl* func, const clang::PrintingPolicy& policy)
        : m_func(func), m_saved(func->getDeclName()) {
        std::string name;
        if (auto* conversion = llvm::dyn_cast<clang::CXXConversionDecl>(func);
            conversion && conversion->getConversionType()->getAs<clang::RecordType>())
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

void DeclBeforeDefVisitor::get_function_declaration(clang::FunctionDecl* func, std::string& decl) {
    llvm::raw_string_ostream decl_out(decl);
    // default arguments are kept on the declaration they were written on as they
    // are still needed in unevaluated contexts that stay generic in the TU (eg.
    // SFINAE checks like `decltype(a.allocate(n))`). A friend declaration with
    // default arguments must be the only declaration and a definition, which no
    // longer holds once its definition is deferred. The same holds for attributes
    // on a friend declaration. Both are kept on the deferred definition.
    clang::PrintingPolicy inplace_policy = m_inplace_policy;
    inplace_policy.SuppressInitializers = func->getFriendObjectKind() != clang::Decl::FOK_None;
    inplace_policy.PolishForDeclaration = func->getFriendObjectKind() != clang::Decl::FOK_None;
    // out-of-line members of class templates carry their `template <...>` header
    // as an outer template parameter list, which the printer emits after the
    // attributes, resulting in `__attribute__((x)) template <...> ...`. The
    // attributes aren't needed on this redeclaration, the in-class declaration
    // and the deferred specializations keep them.
    if (!func->getDescribedFunctionTemplate() && func->getNumTemplateParameterLists() > 0) {
        inplace_policy.PolishForDeclaration = true;
    }
    InheritedDefaultArgsHider hider(func);
    ConversionNameSpeller speller(func, inplace_policy);
    if (func->getFriendObjectKind() != clang::Decl::FOK_None) {
        clang::FriendDecl* friend_decl = get_friend_declaration(func);
        friend_decl->print(decl_out, inplace_policy);
        decl_out.flush();

        // clang printer prints the friend keyword before the 
        // template<> syntax which results in an unparsable TU. 
        // Hence if the friend function is templated, move the friend
        // keyword after the template syntax
        if (func->getDescribedFunctionTemplate()) {
            fix_friend_declaration(decl, func->getDescribedFunctionTemplate(), inplace_policy);
        }
        // a friend naming a specialization, eg. `friend void h<>(S);`, is printed
        // as `friend template<> void h<>(S)`, but a friend declaration can't carry
        // a template header.
        if (func->isFunctionTemplateSpecialization()) {
            size_t pos = decl.find("friend template<> ");
            if (pos != std::string::npos) decl.erase(pos + strlen("friend "), strlen("template<> "));
        }
    } else if (auto *function_template = func->getDescribedFunctionTemplate()) {
        function_template->print(decl_out, inplace_policy);
    } else {
        func->print(decl_out, inplace_policy);
    }
    decl_out.flush();
    if (func->isCXXClassMember() && func->isOutOfLine()) {
        stripRecordDeclOnlySpecifiers(decl);
    }
}

clang::SourceLocation
DeclBeforeDefVisitor::expand_range_to_include_template_headers(clang::SourceLocation begin) {
    clang::SourceManager &source_mgr = m_ctx.getSourceManager();
    const char *buffer_start = source_mgr.getCharacterData(source_mgr.getLocForStartOfFile(source_mgr.getFileID(begin)));
    const char *cursor = source_mgr.getCharacterData(begin);

    for (;;) {
        const char *scan = cursor;
        while (scan > buffer_start && isspace((unsigned char)scan[-1]))
            --scan;
        if (scan == buffer_start || scan[-1] != '>')
            break;

        int angle_depth = 0;
        while (scan > buffer_start) {
            --scan;
            if (*scan == '>') ++angle_depth;
            else if (*scan == '<' && --angle_depth == 0) break;
        }
        if (*scan != '<')
            break;

        while (scan > buffer_start && isspace((unsigned char)scan[-1]))
            --scan;
        if (scan - buffer_start < 8 || strncmp(scan - 8, "template", 8) != 0)
            break;

        cursor = scan - 8;
    }

    return begin.getLocWithOffset(cursor - source_mgr.getCharacterData(begin));
}

// clang reports a truncated end location for a function declaration whose trailing
// return type is a `decltype(...)`: the range ends at the function name. For
// declarations without a body, walk forward to the terminating `;` so that the whole
// declaration is replaced. For correct ranges this is a no-op. Brackets are tracked
// as a `decltype` may contain a lambda with its own `;`.
clang::SourceLocation DeclBeforeDefVisitor::expand_range_to_terminating_semi(clang::SourceLocation end) {
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

void DeclBeforeDefVisitor::replace_function_declaration(clang::FunctionDecl* func, std::string& decl) {
    respellDecayedVaList(decl);
    hoistTrailingAttributes(decl);
    if (func->getFriendObjectKind() != clang::Decl::FOK_None) {
        clang::FriendDecl* friend_decl = get_friend_declaration(func);
        clang::SourceRange range = friend_decl->getSourceRange();
        if (!func->doesThisDeclarationHaveABody())
            range.setEnd(expand_range_to_terminating_semi(range.getEnd()));
        m_rewriter.ReplaceText(range, decl + ";");
    } else if (auto *function_template = func->getDescribedFunctionTemplate()) {
        /**
         * A function can be declared using a nested template level. This happens
         * when along with the function, the class which its a part of is templated
         * as well. However these extra templates do not show up in the AST at all
         * and thus the sourceLocation does not reflect this. We manually find the
         * level of nesting and extend sourceLocation for a proper replacement. 
         * For eg. 
         * template<typename T>
            class hey {
            public:
                template<typename U>
                void foo();
            };

            template<typename T>
                template<typename U>
                    void hey<T>::foo() {
                        T a;
                        U b;
         *          }
         * Note that friendDecls can be templated as well but will never have 
         * this kind of nested template chain. Out of Line declarations will always
         * have only 1 level of template specifier. 
         */
        clang::SourceRange corrected_source_range;
        corrected_source_range.setEnd(function_template->getSourceRange().getEnd());
        if (!func->doesThisDeclarationHaveABody())
            corrected_source_range.setEnd(expand_range_to_terminating_semi(corrected_source_range.getEnd()));
        if (func->isCXXClassMember() and func->isOutOfLine()) {
            corrected_source_range.setBegin(expand_range_to_include_template_headers(function_template->getSourceRange().getBegin()));
        } else {
            corrected_source_range.setBegin(function_template->getSourceRange().getBegin());
        }
        // an abbreviated function template, eg. `void f(auto x)`, has no template head
        if (corrected_source_range.getBegin().isInvalid())
            corrected_source_range.setBegin(func->getSourceRange().getBegin());
        m_rewriter.ReplaceText(corrected_source_range, decl + ";");
    } else {
        clang::SourceRange range = func->getSourceRange();
        if (!func->doesThisDeclarationHaveABody())
            range.setEnd(expand_range_to_terminating_semi(range.getEnd()));
        m_rewriter.ReplaceText(range, decl + ";");
    }
}

/**
 * This pass also takes care of specializing function templates.
 * We don't specialize class templates because that is just a type 
 * at the end of the day. The way we do that is by enabling the visitor
 * to traverse the template instantiations as well. Without this, the visitor
 * would have only traversed the core structure of the templated function
 * and not its specializations. Cases where a function might be partially 
 * specialized won't have a body (check the ast) and we have an early check
 * for that. So we have two cases to deal with, generic template body and
 * their specialized versions. The former is reduced to a declaration
 * and the latter's definition is stored and emitted at the end of the TU.
 */
bool DeclBeforeDefVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (isKeptByDeclBeforeDef(func)) return true;
    // only re-emit declarations at the global level. Declarations
    // inside functions are re-emitted when the function defs are printed
    if (isInsideFunction(func)) return true;
    if (!func->hasBody() || !func->isThisDeclarationADefinition()) {
        // re-emit each function declaration so that if it uses
        // any global variables, the renamed versions of those are printed. 
        // it is possible for a declaration to not have a body
        // and still have instantiations. consider : 
        // template<typename T>
        // void foo();
        // <in some function> foo<int>();
        // instantiated copies are not re-emitted, hence not printed either.
        if (!isInstantiatedCopy(func)) {
            std::string decl;
            get_function_declaration(func, decl);
            replace_function_declaration(func, decl);
        }
        return true;
    }

    std::string decl;
    std::string deferred_decl;
    // declaration to be emitted not as part of the definition
    // but before it. See getDeferredDefinitions. 
    std::string deferred_decl_before_def;

    if (!func->hasWrittenPrototype() && func->getNumParams() > 0) {
        get_kr_function_declarations(func, decl, deferred_decl);
        deferred_decl_before_def = deferred_decl;
    } else {
        if (!isInstantiatedCopy(func)) get_function_declaration(func, decl);

        if (!func->isDependentContext()) {
            ConversionNameSpeller speller(func, m_deferred_policy_decl);
            llvm::raw_string_ostream deferred_decl_out(deferred_decl);
            func->print(deferred_decl_out, m_deferred_policy_decl);
            deferred_decl_out.flush();

            llvm::raw_string_ostream deferred_decl_before_def_out(deferred_decl_before_def);
            func->print(deferred_decl_before_def_out, m_deferred_policy_decl_before_def);
            deferred_decl_before_def_out.flush();

            // a specialization is already constrained by its template and can't
            // repeat the trailing requires clause.
            if (const clang::Expr* constraint = func->getTrailingRequiresClause().ConstraintExpr) {
                auto erase_requires = [&](std::string& printed, clang::PrintingPolicy policy) {
                    policy.SuppressSpecifiers = false;
                    std::string clause = " requires ";
                    llvm::raw_string_ostream clause_out(clause);
                    constraint->printPretty(clause_out, nullptr, policy, 0, "\n", &m_ctx);
                    size_t pos = printed.rfind(clause);
                    if (pos != std::string::npos) printed.erase(pos, clause.size());
                };
                erase_requires(deferred_decl, m_deferred_policy_decl);
                erase_requires(deferred_decl_before_def, m_deferred_policy_decl_before_def);
            }
            hoistTrailingAttributes(deferred_decl);
            hoistTrailingAttributes(deferred_decl_before_def);

            // the `noexcept(expr)` of a specialization is spelled by its value, the
            // expression may call what is only defined later, eg. with a deduced return type
            const auto* proto = func->getType()->getAs<clang::FunctionProtoType>();
            if (func->isTemplateInstantiation() && proto && clang::isComputedNoexcept(proto->getExceptionSpecType()) &&
                proto->getNoexceptExpr()) {
                auto spell_noexcept = [&](std::string& printed, const clang::PrintingPolicy& policy) {
                    std::string spec = " noexcept(";
                    llvm::raw_string_ostream spec_out(spec);
                    proto->getNoexceptExpr()->printPretty(spec_out, nullptr, policy, 0, "\n", &m_ctx);
                    spec_out << ")";
                    size_t pos = printed.rfind(spec);
                    if (pos != std::string::npos)
                        printed.replace(pos, spec.size(), proto->isNothrow() ? " noexcept" : " noexcept(false)");
                };
                spell_noexcept(deferred_decl, m_deferred_policy_decl);
                spell_noexcept(deferred_decl_before_def, m_deferred_policy_decl_before_def);
            }

            // the printer prints a deduced return type (eg. `auto`, `decltype(auto)` or
            // `Small auto`) as the type it was deduced to. For template instantiations
            // this doesn't match the primary template, which stays generic in the TU:
            // a specialization of a function declared with a placeholder return type
            // must repeat that exact placeholder. Hence the deduced return type is
            // replaced back with the return type as spelled in the pattern.
            // Non-template functions keep the deduced type, as every declaration of
            // them is re-printed with it.
            auto* auto_type = func->getReturnType()->getContainedAutoType();
            const clang::FunctionDecl* pattern = func->getTemplateInstantiationPattern();
            if (func->isTemplateInstantiation() && auto_type && auto_type->isDeduced() && pattern) {
                std::string deduced = func->getReturnType().getAsString(m_deferred_policy_decl);
                std::string placeholder = pattern->getReturnType().getAsString(m_deferred_policy_decl);
                auto restore_placeholder = [&](std::string& decl) {
                    // the return type is outside of the qualifier's template arguments and the parameters
                    auto ident = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
                    size_t pos = std::string::npos;
                    int depth = 0;
                    for (size_t i = 0; i < decl.size() && pos == std::string::npos; ++i) {
                        if (decl[i] == '<' || decl[i] == '(') ++depth;
                        else if ((decl[i] == '>' && decl[i - 1] != '-') || decl[i] == ')') --depth;
                        else if (depth == 0 && decl.compare(i, deduced.size(), deduced) == 0 &&
                                 (i == 0 || (!ident(decl[i - 1]) && decl[i - 1] != ':')) &&
                                 (i + deduced.size() == decl.size() || !ident(deduced.back()) ||
                                  (!ident(decl[i + deduced.size()]) && decl[i + deduced.size()] != ':')))
                            pos = i;
                    }
                    if (pos == std::string::npos) return;
                    decl.replace(pos, deduced.size(), placeholder);
                    // `int &get()` has no space after the deduced type, `decltype(auto) get()` needs one.
                    size_t end = pos + placeholder.size();
                    if (end < decl.size() && (isalnum((unsigned char)decl[end]) || decl[end] == '_') &&
                        (isalnum((unsigned char)placeholder.back()) || placeholder.back() == ')'))
                        decl.insert(end, " ");
                };
                restore_placeholder(deferred_decl);
                restore_placeholder(deferred_decl_before_def);
            }

            // the template arguments of a specialization are substituted into every
            // template of its name, which fails hard for some operators, eg. for the
            // `operator+` of two durations given the ones of a time_point and a duration.
            // Those of an operator are deduced from its parameters instead when they can be.
            llvm::SmallBitVector deducible;
            if (const clang::FunctionTemplateDecl* primary = func->getPrimaryTemplate();
                primary && func->isOverloadedOperator()) {
                clang::Sema::MarkDeducedTemplateParameters(m_ctx, primary, deducible);
                std::string name = std::string("operator") + clang::getOperatorSpelling(func->getOverloadedOperator()) + "<";
                auto strip_arguments = [&](std::string& decl) {
                    size_t open = decl.find(name);
                    if (open == std::string::npos) return;
                    open += name.size() - 1;
                    int depth = 0;
                    for (size_t close = open; close < decl.size(); ++close) {
                        if (decl[close] == '<' || decl[close] == '(') ++depth;
                        else if ((decl[close] == '>' || decl[close] == ')') && --depth == 0) {
                            decl.erase(open, close + 1 - open);
                            return;
                        }
                    }
                };
                if (deducible.all()) {
                    strip_arguments(deferred_decl);
                    strip_arguments(deferred_decl_before_def);
                }
            }
        }
    }

    // If a functionDecl is not dependent, it is callable
    // within the TU and hence we record its definition to emit
    // it at the end of the TU. 
    if (!func->isDependentContext()) {
        record_function_definition(func, deferred_decl, deferred_decl_before_def);
    }

    // Removes all in-place function definitions.
    // The guard ensures that edits do not overlap. Template
    // instantiations will point to the same sourceLocation
    // and hence an edit through only of them will suffice. 
    // We choose the primaryTemplate (uninstantiated) for this. 
    // Untemplated functions and explicit function template specializations
    // undergoes edit through this only once.
    if (!isInstantiatedCopy(func)) {
        replace_function_declaration(func, decl);
    }
    return true;
}

// a specialization whose return type is deduced keeps the `auto` of its template and
// can't be called before it is defined. These are defined first, each after the
// ones it calls, the others follow in order.
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
    for (size_t i = 0; i < m_deferred_functions.size(); ++i)
        if (!placed[i]) order.push_back(i);
    return order;
}

std::string DeclBeforeDefVisitor::getDeferredDefinitions() {
    // emitting declarations for deferred definitions beforehand
    // helps avoiding emitting specializations after instantiations. 
    // If we do not emit declarations, its possible one function definition
    // instantiates a template, but a template specialization of that is present
    // after this definition. Eg. : 
    /*
        template<typename T>
        int foo();

        template<typename U>
        int bar() {
            U a;
            U b;
        }

        template<typename T>
        int foo() {
            bar<T>();
        }

        int zed() {
            foo<int>();
        }
    */

    std::string all_decls;
    size_t total = 1;
    for (const auto& decl : m_deferred_declarations) {
        total += decl.size() + 1;
    }
    all_decls.reserve(total);

    all_decls += '\n';
    for(auto& decl : m_deferred_declarations) {
        all_decls += std::move(decl);
        all_decls += "\n";
    }

    std::string all_defs;
    total = 1;
    for (const auto& def : m_deferred_definitions)
        total += def.size() + 1;
    for (const auto& def : m_undeclared_definitions)
        total += def.size() + 1;
    all_defs.reserve(total);

    all_defs += '\n';
    for (auto& def : m_undeclared_definitions) {
        all_defs += std::move(def);
        all_defs += "\n";
    }
    for (size_t i : deferredDefinitionOrder()) {
        auto& def = m_deferred_definitions[i];
        all_defs += std::move(def);
        all_defs += "\n";
    }

    std::string result = all_decls + all_defs;
    respellDecayedVaList(result);
    return result;
}

}  // namespace invisible
