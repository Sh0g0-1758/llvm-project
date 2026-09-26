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

// ─── DeclBeforeDefVisitor ──────────────────────────────────────────────

bool DeclBeforeDefVisitor::shouldVisitTemplateInstantiations() const { return true; }

DeclBeforeDefVisitor::DeclBeforeDefVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_deferred_policy_decl(m_ctx.getLangOpts()), m_deferred_policy_ctor(m_ctx.getLangOpts()), m_deferred_policy_decl_before_def(m_ctx.getLangOpts()), m_inplace_policy(m_ctx.getLangOpts()), m_custom_printer(ctx, m_policy) {
    if (m_ctx.getLangOpts().CPlusPlus) {
        m_policy.adjustForCPlusPlus();
    }
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;


    m_deferred_policy_ctor = m_policy;
    m_deferred_policy_ctor.PrintAsCanonical = true;
    m_deferred_policy_ctor.FullyQualifiedName = true;
    m_deferred_policy_ctor.SuppressTagKeyword = true;


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
    func->getBody()->printPretty(def_out, &m_custom_printer, m_policy);
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

    if (auto primary_template = func->getPrimaryTemplate()) {
        if (primary_template->getTemplateParameters()->hasParameterPack()) {
            fixParameterPack(deferred_decl, func);
            fixParameterPack(deferred_decl_before_def, func);
        }
    }

    if (m_ctx.getLangOpts().CPlusPlus && llvm::isa<clang::CXXMethodDecl>(func)) {
        stripRecordDeclOnlySpecifiers(deferred_decl);
        stripRecordDeclOnlySpecifiers(deferred_decl_before_def);
    }

    m_deferred_definitions.push_back(prefix + deferred_decl + ctor_init + def);
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

void DeclBeforeDefVisitor::fix_friend_declaration(std::string& decl) {
    size_t lt = decl.find('<');
    int depth = 0;
    size_t i = lt;
    for (; i < decl.size(); ++i) {
        if (decl[i] == '<') ++depth;
        else if (decl[i] == '>' && --depth == 0) break;
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
    }

    ~InheritedDefaultArgsHider() {
        for (const auto& [param, arg, uninstantiated] : m_hidden) {
            if (uninstantiated) param->setUninstantiatedDefaultArg(arg);
            else param->setDefaultArg(arg);
        }
    }

  private:
    struct Hidden { clang::ParmVarDecl* param; clang::Expr* arg; bool uninstantiated; };
    std::vector<Hidden> m_hidden;
};

void DeclBeforeDefVisitor::get_function_declaration(clang::FunctionDecl* func, std::string& decl) {
    llvm::raw_string_ostream decl_out(decl);
    // default arguments are kept on the declaration they were written on as they
    // are still needed in unevaluated contexts that stay generic in the TU (eg.
    // SFINAE checks like `decltype(a.allocate(n))`). A friend declaration with
    // default arguments must be the only declaration and a definition, which no
    // longer holds once its definition is deferred.
    clang::PrintingPolicy inplace_policy = m_inplace_policy;
    inplace_policy.SuppressInitializers = func->getFriendObjectKind() != clang::Decl::FOK_None;
    InheritedDefaultArgsHider hider(func);
    if (func->getFriendObjectKind() != clang::Decl::FOK_None) {
        clang::FriendDecl* friend_decl = get_friend_declaration(func);
        friend_decl->print(decl_out, inplace_policy);
        decl_out.flush();

        // clang printer prints the friend keyword before the 
        // template<> syntax which results in an unparsable TU. 
        // Hence if the friend function is templated, move the friend
        // keyword after the template syntax
        if (func->getDescribedFunctionTemplate()) {
            fix_friend_declaration(decl);
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

void DeclBeforeDefVisitor::replace_function_declaration(clang::FunctionDecl* func, std::string& decl) {
    if (func->getFriendObjectKind() != clang::Decl::FOK_None) {
        clang::FriendDecl* friend_decl = get_friend_declaration(func);
        m_rewriter.ReplaceText(friend_decl->getSourceRange(), decl + ";");
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
        if (func->isCXXClassMember() and func->isOutOfLine()) {
            corrected_source_range.setBegin(expand_range_to_include_template_headers(function_template->getSourceRange().getBegin()));
        } else {
            corrected_source_range.setBegin(function_template->getSourceRange().getBegin());
        }
        m_rewriter.ReplaceText(corrected_source_range, decl + ";");
    } else {
        m_rewriter.ReplaceText(func->getSourceRange(), decl + ";");
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
    if (func->isDefaulted() || func->isDeleted()) return true;
    // only re-emit declarations at the global level. Declarations
    // inside functions are re-emitted when the function defs are printed
    if (isInsideFunction(func)) return true;
    if (!func->hasBody() || !func->isThisDeclarationADefinition()) {
        // re-emit each function declaration so that if it uses
        // any global variables, the renamed versions of those are printed. 
        std::string decl;
        get_function_declaration(func, decl);
        // it is possible for a declaration to not have a body
        // and still have instantiations. consider : 
        // template<typename T>
        // void foo();
        // <in some function> foo<int>();
        if (!isInstantiatedCopy(func)) {
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
        get_function_declaration(func, decl);

        if (!func->isDependentContext()) {
            llvm::raw_string_ostream deferred_decl_out(deferred_decl);
            func->print(deferred_decl_out, m_deferred_policy_decl);
            deferred_decl_out.flush();

            llvm::raw_string_ostream deferred_decl_before_def_out(deferred_decl_before_def);
            func->print(deferred_decl_before_def_out, m_deferred_policy_decl_before_def);
            deferred_decl_before_def_out.flush();

            // when printing declarations with the auto keyword, the printer will try 
            // to print the deduced type. This can lead to errors if one declaration has the
            // auto keyword and the other have its deduced form. Two declarations can only
            // differ in the return type deduction if one of them has more information about 
            // its type than the other. Such a case is only true for template instantiations. 
            // Hence if a declaration is an instantiation and its return type has the auto keyword
            // and its deduced, revert it back to the auto keyword to maintain uniformity. 
            auto* auto_type = func->getReturnType()->getContainedAutoType();
            if (func->isTemplateInstantiation() && auto_type && !auto_type->getDeducedType().isNull()) {
                std::string deduced;
                llvm::raw_string_ostream deduced_out(deduced);
                auto_type->getDeducedType().print(deduced_out, m_deferred_policy_decl);
                deduced_out.flush();

                size_t pos = deferred_decl.find(deduced);
                if (pos != std::string::npos)
                    deferred_decl.replace(pos, deduced.size(), "auto");

                pos = deferred_decl_before_def.find(deduced);
                if (pos != std::string::npos)
                    deferred_decl_before_def.replace(pos, deduced.size(), "auto");
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
    all_defs.reserve(total);

    all_defs += '\n';
    for (auto& def : m_deferred_definitions) {
        all_defs += std::move(def);
        all_defs += "\n";
    }

    return all_decls + all_defs;
}

}  // namespace invisible
