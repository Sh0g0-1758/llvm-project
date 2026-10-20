#include "inline/InlineVisitor.h"
#include "helpers.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/JSON.h"
#include "clang/AST/TemplateBase.h"
#include "llvm/Support/Regex.h"

#include <string>
#include <queue>
#include <stack>

namespace invisible {

// --- FunctionParamRenamer -------------------------------------------

FunctionParamRenamer::FunctionParamRenamer(clang::ASTContext &C, std::map<int, std::string> &PR)
    : Ctx(C), callerParams(PR) {}

// `x * x` with `x` the first parameter becomes `__inline_param__3_0 * __inline_param__3_0`: the
// parameter itself is renamed, so every reference to it prints the new name
bool FunctionParamRenamer::VisitDeclRefExpr(clang::DeclRefExpr *DRE) {
    if (clang::VarDecl *VD = llvm::dyn_cast<clang::VarDecl>(DRE->getDecl()))
        if (clang::ParmVarDecl *PVD = llvm::dyn_cast<clang::ParmVarDecl>(VD))
            VD->setDeclName(clang::DeclarationName(&Ctx.Idents.get(callerParams[PVD->getFunctionScopeIndex()])));
    return true;
}

// --- InlinePrinterHelper -------------------------------------------

InlinePrinterHelper::InlinePrinterHelper(clang::ASTContext& ctx, std::string target_var, std::string bind_type,
                                         std::map<int, std::string> caller_params, clang::PrintingPolicy policy,
                                         size_t current_index)
    : m_ctx(ctx),
      m_target_var(std::move(target_var)),
      m_bind_type(std::move(bind_type)),
      m_caller_params(std::move(caller_params)),
      m_policy(policy), 
      m_current_index(current_index), 
      m_goto_labels({}),
      m_func_param_renamer(m_ctx, m_caller_params),
      m_goto_label_index(0) {
        if (m_ctx.getLangOpts().CPlusPlus)
            m_policy.adjustForCPlusPlus();
        m_policy.IncludeTagDefinition = true;
      }

// The parameters a type names are in its expressions, which no DeclRefExpr of the body reaches,
// eg. `n` in `int buf[n];` or in `typeof(n + 1) t;`. The type is walked down to them.
void InlinePrinterHelper::renameFuncParamInType(clang::QualType T) {
    const clang::Type *TP = T.getTypePtr();

    if (const auto *TOE = llvm::dyn_cast<clang::TypeOfExprType>(TP)) {
        if (clang::Expr *e = TOE->getUnderlyingExpr()) {
            m_func_param_renamer.TraverseStmt(e);
        }
        renameFuncParamInType(TOE->desugar());
        return;
    } else if (const auto *TOT = llvm::dyn_cast<clang::TypeOfType>(TP)) {
        renameFuncParamInType(TOT->desugar());
        return;
    }

    if (const auto *VAT = llvm::dyn_cast<clang::VariableArrayType>(TP)) {
        if (clang::Expr *size = VAT->getSizeExpr())
            m_func_param_renamer.TraverseStmt(size);
        renameFuncParamInType(VAT->getElementType());
    } else if (const auto *PT = llvm::dyn_cast<clang::PointerType>(TP)) {
        renameFuncParamInType(PT->getPointeeType());
    } else if (const auto *RT = llvm::dyn_cast<clang::ReferenceType>(TP)) {
        renameFuncParamInType(RT->getPointeeType());
    } else if (const auto *AT = llvm::dyn_cast<clang::ArrayType>(TP)) {
        renameFuncParamInType(AT->getElementType());
    } else if (const auto *FPT = llvm::dyn_cast<clang::FunctionProtoType>(TP)) {
        renameFuncParamInType(FPT->getReturnType());
        for (auto paramType : FPT->param_types())
            renameFuncParamInType(paramType);
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        renameFuncParamInType(FT->getReturnType());
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        renameFuncParamInType(FT->getReturnType());
    } else if (const auto *MPT = llvm::dyn_cast<clang::MemberPointerType>(TP)) {
        renameFuncParamInType(MPT->getPointeeType());
    } else if (const auto *VT = llvm::dyn_cast<clang::VectorType>(TP)) {
        renameFuncParamInType(VT->getElementType());
    } else if (const auto *ParenT = llvm::dyn_cast<clang::ParenType>(TP)) {
        renameFuncParamInType(ParenT->getInnerType());
    }
}

// Called for every statement and expression printed. It renames what the printer would print
// with a parameter's name and lets the printer go on (returns false), or prints the statement
// itself (returns true), eg. a return.
bool InlinePrinterHelper::handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) {
    // a parameter, `x`, is printed as the variable it is bound to, `__inline_param__3_0`
    if (auto* decl_ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt)) {
        m_func_param_renamer.TraverseStmt(decl_ref);
        return false;
    }

    if (auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(stmt)) {
        for (auto it = decl_stmt->decl_begin(); it != decl_stmt->decl_end(); ++it) {
            if (auto* var_decl = llvm::dyn_cast<clang::VarDecl>(*it)) {
                // a parameter named in the variable's type, eg. `n` of `int buf[n];` or `typeof(n) t;`
                renameFuncParamInType(var_decl->getType());

                // and in its initializer, eg. `int y = n * 2;`
                if (clang::Expr *init = var_decl->getInit())
                    m_func_param_renamer.TraverseStmt(init);
            }
        }
        // the printer then prints the declaration itself, as it handles `int a, b;` and the init of a
        // for. The printer doesn't ask the helper about a type or an initializer, so the parameters in
        // them are renamed in the AST beforehand: a renamed parameter is renamed wherever it is
        // shared, which is what every use of it in the body needs anyway
        return false;
    }

    // the init statements of for, if and switch are printed without asking the helper,
    // eg. `for (int i = n; ...)`, so the parameters in them are renamed beforehand
    if (auto* for_stmt = llvm::dyn_cast<clang::ForStmt>(stmt)) {
        if (auto* init = for_stmt->getInit())
            m_func_param_renamer.TraverseStmt(init);
        return false;
    }

    if (auto* if_stmt = llvm::dyn_cast<clang::IfStmt>(stmt)) {
        if (auto* init = if_stmt->getInit())
            m_func_param_renamer.TraverseStmt(init);
        return false;
    }

    if (auto* switch_stmt = llvm::dyn_cast<clang::SwitchStmt>(stmt)) {
        if (auto* init = switch_stmt->getInit())
            m_func_param_renamer.TraverseStmt(init);
        return false;
    }

    // `asm volatile("..." : "=r"(x))` is printed `__asm__ volatile(...)`: `asm` isn't a
    // keyword in the strict C modes, eg. -std=c11, where the inlined code is compiled
    if (auto* gcc_asm = llvm::dyn_cast<clang::GCCAsmStmt>(stmt)) {
        m_func_param_renamer.TraverseStmt(gcc_asm);

        std::string asm_stmt;
        llvm::raw_string_ostream asm_out(asm_stmt);
        gcc_asm->printPretty(asm_out, nullptr, m_policy);
        asm_out.flush();

        asm_stmt.replace(0, 3, "__asm__");
        stream << asm_stmt << ";";
        return true;
    }

    // a return leaves the inlined body, through the label after it
    if (auto* return_stmt = llvm::dyn_cast<clang::ReturnStmt>(stmt)) {
        std::string exit = "goto __inline_exit_" + std::to_string(m_current_index) + ";";
        clang::Expr* ret_val = return_stmt->getRetValue();
        if (!ret_val) {
            stream << exit << "\n";
            return true;
        }
        stream << "{ ";
        // a returned reference is bound as the return statement binds it, and kept by its address
        if (!m_bind_type.empty())
            stream << m_bind_type << " = ";
        else if (!m_target_var.empty())
            stream << m_target_var << " = ";
        ret_val->printPretty(stream, this, m_policy);
        if (!m_bind_type.empty())
            stream << "; " << m_target_var << " = __builtin_addressof(__inline_ref__)";
        stream << "; " << exit << " }\n";
        return true;
    }

    // `__func__` names the function it is written in, which is the callee
    if (auto* predefined = llvm::dyn_cast<clang::PredefinedExpr>(stmt)) {
        if (clang::StringLiteral* name = predefined->getFunctionName()) {
            name->printPretty(stream, nullptr, m_policy);
            return true;
        }
    }

    return false;
}

// --- LabelMaker ---------------------------

// Renames the labels of a callee's body to labels of its own site, eg. `retry:` to
// `__inline_label_3__0`: the same body pasted twice into one caller, or next to a label of the
// caller of the same name, would otherwise define a label twice.
class LabelMaker : public clang::RecursiveASTVisitor<LabelMaker> {
private:
    clang::ASTContext& m_ctx;
    size_t m_current_index;
    std::map<std::string, std::string> m_goto_labels;
    size_t m_goto_label_index;
public:
    LabelMaker(clang::ASTContext& ctx, size_t current_index) : m_ctx(ctx),
                                                               m_current_index(current_index), 
                                                               m_goto_labels({}),
                                                               m_goto_label_index(0) { }

    bool VisitLabelStmt(clang::LabelStmt* label_stmt) {
        clang::LabelDecl *decl = label_stmt->getDecl();
        clang::IdentifierInfo &newII = m_ctx.Idents.get(std::string("__inline_label_") + std::to_string(m_current_index) + "__" + std::to_string(m_goto_label_index++));
        decl->setDeclName(clang::DeclarationName(&newII));

        return true;
    }
};

// --- InlineVisitor -------------------------------------------------

InlineVisitor::InlineVisitor(clang::ASTContext& ctx, const std::string& caller, const std::string& callee,
                             std::vector<size_t>& call_sites, size_t& current_index,
                             clang::Rewriter& rewriter, std::map<std::string, clang::FunctionDecl*>& decl_map,
                             std::map<const clang::Stmt*, clang::VarDecl*>& this_vars)
    : m_ctx(ctx),
      m_caller(caller),
      m_callee(callee),
      m_call_sites(call_sites),
      m_current_index(current_index),
      m_rewriter(rewriter),
      m_policy(m_ctx.getLangOpts()),
      m_names(ctx),
      m_function_decl_map(decl_map),
      m_this_vars(this_vars) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
}

// Whether @p func is the oracle's caller. The oracle names functions as LLVM does, by mangled
// name. A constructor or destructor is emitted as several functions, one per variant, and matches
// by any of their names, eg. the oracle may name the complete object constructor `_ZN3VecC1Ev` or
// the base object one `_ZN3VecC2Ev` of `Vec::Vec()`. A template isn't emitted, only its
// instantiations, so it is never the caller.
bool InlineVisitor::isCaller(const clang::FunctionDecl* func) {
    if (func->isDependentContext()) return false;
    if (llvm::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl>(func))
        return llvm::is_contained(m_names.getAllManglings(func), m_caller);
    return m_names.getName(func) == m_caller;
}

// Whether @p stmt is written in the caller: the innermost function around it must be the caller.
// A call on the right line can be in another function written inside the caller, eg. `square(2)`
// in `void total() { struct L { int g() { return square(2); } }; ... }` is in `L::g`, whose
// call LLVM reports with `L::g` as the caller.
bool InlineVisitor::inCaller(const clang::Stmt* stmt) {
    for (auto parents = m_ctx.getParents(*stmt); !parents.empty(); parents = m_ctx.getParents(parents[0]))
        if (const auto* func = parents[0].get<clang::FunctionDecl>())
            return isCaller(func);
    return false;
}

// Builds a CallIndex. Every oracle entry of a round is looked up in the same parse, and looking
// each one up by walking the whole TU would be quadratic, so the TU is walked once and what an
// entry needs is kept by key:
//  - the calls on each line, the candidates for the entry's site.
//  - the mangled names of the constructors and destructors, which aren't inlined. 
class CallIndexer : public clang::RecursiveASTVisitor<CallIndexer> {
  public:
    CallIndexer(clang::ASTContext& ctx, CallIndex& index) : m_ctx(ctx), m_index(index), m_names(ctx) {}
    bool shouldVisitImplicitCode() const { return true; }
    bool VisitFunctionDecl(clang::FunctionDecl* func) {
        if (func->isDependentContext() || llvm::isa<clang::CXXDeductionGuideDecl>(func)) return true;
        if (llvm::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl>(func)) {
            std::vector<std::string> names = m_names.getAllManglings(func);
            m_index.structors.insert(names.begin(), names.end());
        }
        return true;
    }
    bool VisitCallExpr(clang::CallExpr* call) {
        auto& src_mgr = m_ctx.getSourceManager();
        m_index.calls[src_mgr.getPresumedLoc(src_mgr.getSpellingLoc(call->getExprLoc())).getLine()].push_back(call);
        return true;
    }

  private:
    clang::ASTContext& m_ctx;
    CallIndex& m_index;
    clang::ASTNameGenerator m_names;
};

CallIndex buildCallIndex(clang::ASTContext& ctx) {
    CallIndex index;
    CallIndexer indexer(ctx, index);
    indexer.TraverseDecl(ctx.getTranslationUnitDecl());
    return index;
}

// Finds the entry's site and inlines it, see VisitCallExpr. A constructor or destructor isn't
// inlined, so an entry whose callee is one is told by the name alone and skipped; a destructor has
// no call written anyway, it runs where a scope ends. Otherwise the calls the index has on the
// line are tried in turn until one is the site.
void InlineVisitor::run(const CallIndex& index) {
    if (index.structors.count(m_callee)) {
        m_matched = true;
        m_skipped = "constructors and destructors aren't inlined";
        return;
    }
    size_t line = m_call_sites[m_current_index];
    if (auto found = index.calls.find(line); found != index.calls.end())
        for (clang::CallExpr* call : found->second) {
            // a call through a function pointer has no callee in the AST
            clang::FunctionDecl* callee = call->getDirectCallee();
            if (!callee)
                callee = m_function_decl_map[m_callee];
            // the call of the callee the oracle names, with a body to inline, written in the caller.
            // Another call on the line isn't it, eg. `foo` in `foo(bar())` where the oracle names
            // `bar` because hoist-calls left the line as written
            if (!callee || !callee->hasBody() || m_names.getName(callee) != m_callee || !inCaller(call))
                continue;
            VisitCallExpr(call, callee);
            if (m_matched) return;
        }
}

bool InlineVisitor::shouldVisitImplicitCode() const { return true; }

// a local static is one object, which every copy of an inlined body would duplicate, eg. the
// counter of `int next() { static int n; return ++n; }` would count per site
class StaticLocalFinder : public clang::RecursiveASTVisitor<StaticLocalFinder> {
  public:
    bool VisitVarDecl(clang::VarDecl* var) {
        m_found = m_found || var->isStaticLocal();
        return !m_found;
    }
    bool m_found = false;
};

// Inlines one call. The callee's body is pasted just before the statement the call is in, and the
// call is replaced with a variable the body stores its result in. The checks come first, see the
// declaration. In the names below, N is the index of the oracle entry, so no two sites share a name.
//
// The shape:
//
// With `int square(int x) { return x * x; }`, the statement `int y = square(a);` becomes
//
//     int __inline_ret__0;                  the result
//     {int __inline_param__0_0 = a;         the parameter, bound to its argument
//     {
//     { __inline_ret__0 = __inline_param__0_0 * __inline_param__0_0; goto __inline_exit_0; }
//     }                                     the body: a return stores the result and jumps out
//     __inline_exit_0: ;}                   the label every return jumps to
//     int y = __inline_ret__0;              the statement, the call replaced by the result
//
// The result:
//
// An assignment ends the same way:
//
//     y = square(b);                  =>    int __inline_ret__1;  {...}  y = __inline_ret__1;
//
// A call returning void has no result, so the block replaces the call itself. The `;` after the
// call is left behind as an empty statement:
//
//     log_it(y);                      =>    {int __inline_param__2_0 = y; ... __inline_exit_2: ;};
//
// A class is declared first and assigned by the return, so it must be trivially default
// constructible and copyable:
//
//     Point m = mid(p, q);            =>    Point __inline_ret__3;
//                                           {... { __inline_ret__3 = {...}; goto __inline_exit_3; } ...}
//                                           Point m = __inline_ret__3;
//
// A const result is kept without the const, as it is assigned:
//
//     const int k = limit();          =>    int __inline_ret__N;  {...}  const int k = __inline_ret__N;
//
// A reference can't be declared first and assigned later, so a returned reference that is bound is
// kept by its address. `__inline_ref__` binds the returned expression as the return would, eg.
// `std::move(x)` to an `int &&`, so that its address can be taken, and the static_cast turns the
// pointer back into the reference:
//
//     int &r = pick(a, b);            =>    int *__inline_ret__4;
//                                           {... { int &__inline_ref__ = a > b ? a : b;
//                                                  __inline_ret__4 = __builtin_addressof(__inline_ref__);
//                                                  goto __inline_exit_4; } ...}
//                                           int &r = static_cast<int &>(*__inline_ret__4);
//
// A returned reference that is only read is kept as a value:
//
//     int c = pick(a, b);             =>    int __inline_ret__5;
//                                           {... { __inline_ret__5 = a > b ? a : b; goto __inline_exit_5; } ...}
//                                           int c = __inline_ret__5;
//
// The parameters:
//
// Each parameter becomes a variable of the block, with the parameter's type. So each argument is
// evaluated once, before the body, and a reference parameter changes the caller's variable:
//
//     pick(a, b)                      =>    int &__inline_param__4_0 = a;
//                                           int &__inline_param__4_1 = b;
//
// A default argument is written out:
//
//     log_it(y)                       =>    int __inline_param__2_0 = y;
//     void log_it(int x, int y = 7)         int __inline_param__2_1 = 7;
//
// A va_list parameter that is an array, as on x86-64, isn't copied, as it is passed as a pointer,
// which va_arg doesn't take: the body uses the argument itself. A parameter named in a type or an
// initializer, eg. `n` of `int buf[n];`, is renamed too, see FunctionParamRenamer.
//
// Member functions:
//
// The object is bound once, before the arguments, to `__inline_this__N`, and `this` in the body is
// replaced with it, eg. `int size() const { return n; }` returns `__inline_this__6->n`:
//
//     v.size()                        =>    auto &&__inline_object__6 = v;
//                                           const Vec *const __inline_this__6 = __builtin_addressof(__inline_object__6);
//     p->size()                       =>    const Vec *const __inline_this__7 = p;
//
// `auto &&` keeps an object that is a temporary alive for the block, eg. the one of `make().size()`.
// A member operator takes its first operand as the object, and the others as its parameters:
//
//     v + *p                          =>    auto &&__inline_object__8 = v;
//                                           const Vec *const __inline_this__8 = __builtin_addressof(__inline_object__8);
//                                           const Vec &__inline_param__8_0 = *p;
//
// The body, see InlinePrinterHelper:
//
//     return e;                       =>    { __inline_ret__N = e; goto __inline_exit_N; }
//     return;                         =>    goto __inline_exit_N;
//     retry: ... goto retry;          =>    __inline_label_N__0: ... goto __inline_label_N__0;    see LabelMaker
//     __func__                        =>    "name"                    the callee's name, not the caller's
//     max(a, b)                       =>    ::util::max(a, b)         qualified, as the body is pasted elsewhere
//     asm("...")                      =>    __asm__("...")            a keyword in every language mode
bool InlineVisitor::VisitCallExpr(clang::CallExpr* call, clang::FunctionDecl* callee) {
    auto& src_mgr = m_ctx.getSourceManager();

    // a member function is called on the object a member expression names, or on the
    // first operand of an operator. A call through a pointer to member has neither.
    auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
    auto* member = llvm::dyn_cast<clang::MemberExpr>(call->getCallee()->IgnoreParens());
    auto* op_call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(call);
    unsigned first_arg = op_call && method ? 1 : 0;
    if (llvm::isa<clang::CXXDestructorDecl>(callee) || (method && method->isInstance() && !member && !first_arg))
        return true;

    // a variadic function isn't inlined: its extra arguments have no parameter to be bound to, and
    // a body that reads them with va_start needs a real call
    if (callee->isVariadic()) {
        m_matched = true;
        m_skipped = "a variadic function isn't inlined";
        return true;
    }

    // every parameter is bound to an argument below, so a call whose arguments don't line up with
    // the parameters isn't taken for the site
    if (call->getNumArgs() != callee->getNumParams() + first_arg)
        return true;

    m_matched = true;
    // the statement the code is pasted before: hoist-calls has made every call it could a statement
    // of its own, `int x = f();`, `x = f();` or `f();` and their variants, see topLevelStatement. A call it left nested,
    // eg. in a member initializer, `: base(f())`, has no place for the code
    const clang::Stmt* stmt = topLevelStatement(m_ctx, call);
    if (!stmt) {
        m_skipped = "the call isn't a statement of its own, which hoist-calls makes every call it can";
        return true;
    }
    // a static or thread local variable is initialized once, the first time its declaration is
    // reached, but the code pasted before it would run every time; a constexpr one is initialized
    // by a constant expression, which the variable holding the result isn't
    if (const auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(stmt)) {
        if (llvm::any_of(decl_stmt->decls(), [](const clang::Decl* decl) {
                const auto* var = llvm::dyn_cast<clang::VarDecl>(decl);
                return var && (var->isStaticLocal() || var->getTLSKind());
            })) {
            m_skipped = "the initializer of a static or thread local variable runs once, the inlined code would run each time";
            return true;
        }
        if (llvm::any_of(decl_stmt->decls(), [](const clang::Decl* decl) {
                const auto* var = llvm::dyn_cast<clang::VarDecl>(decl);
                return var && var->isConstexpr();
            })) {
            m_skipped = "the initializer of a constexpr variable must be a constant expression";
            return true;
        }
    }
    StaticLocalFinder statics;
    statics.TraverseStmt(callee->getBody());
    if (statics.m_found) {
        m_skipped = "a function with a static local variable isn't inlined";
        return true;
    }

    // the body is looked up where it is inlined, so its names are qualified, and
    // its types printed canonically, with their scopes
    clang::PrintingPolicy policy = m_policy;
    if (m_ctx.getLangOpts().CPlusPlus) {
        policy.PrintAsCanonical = true;
        policy.SuppressUnwrittenScope = true;
    }

    // The result is kept in a variable declared before the inlined body, which the call
    // is replaced with. A returned reference is kept as a pointer, unless the call is
    // only read, and a class result only when it is trivial, being assigned.
    std::string index = std::to_string(m_current_index);
    std::string result = "__inline_ret__" + index;
    clang::QualType returned = callee->getReturnType();
    // `f();` returning void is replaced whole by the inlined code; a result is kept otherwise
    bool discarded = call->getType()->isVoidType();
    bool reference = returned->isReferenceType();
    if (reference && !discarded) {
        const clang::Stmt* use = call;
        for (auto parents = m_ctx.getParents(*use); !parents.empty() && parents[0].get<clang::ParenExpr>();
             parents = m_ctx.getParents(*use))
            use = parents[0].get<clang::ParenExpr>();
        auto parents = m_ctx.getParents(*use);
        const auto* cast = parents.empty() ? nullptr : parents[0].get<clang::ImplicitCastExpr>();
        if (cast && cast->getCastKind() == clang::CK_LValueToRValue) {
            reference = false;
            returned = returned.getNonReferenceType();
        }
    }
    clang::QualType stored = reference ? m_ctx.getPointerType(returned.getNonReferenceType()) : returned.getUnqualifiedType();
    if (const auto* record = stored->getAsCXXRecordDecl();
        record && !(record->hasTrivialDefaultConstructor() && record->isTriviallyCopyable())) {
        m_skipped = "a call returning a class that isn't trivially default constructible and copyable isn't inlined";
        return true;
    }
    bool keeps_result = !discarded && !callee->getReturnType()->isVoidType();

    std::string inlined_code{};
    llvm::raw_string_ostream OS(inlined_code);
    if (keeps_result) {
        stored.print(OS, policy, result);
        OS << ";\n";
    }

    OS << "{";

    // the object is bound once, before the arguments, and `this` in the body names it
    if (method && method->isInstance()) {
        const clang::Expr* object = first_arg ? call->getArg(0) : member->getBase();
        bool arrow = !first_arg && member->isArrow();
        std::string object_text;
        llvm::raw_string_ostream object_out(object_text);
        object->printPretty(object_out, nullptr, m_policy);
        std::string this_name = "__inline_this__" + index;
        if (!arrow) {
            OS << "auto &&__inline_object__" << index << " = " << object_text << ";\n";
            object_text = "__builtin_addressof(__inline_object__" + index + ")";
        }
        method->getThisType().withConst().print(OS, policy, this_name);
        OS << " = " << object_text << ";\n";
        // a body inlined again names the variable its `this` was replaced with as this site's
        clang::VarDecl*& this_var = m_this_vars[callee->getBody()];
        if (this_var) {
            this_var->setDeclName(&m_ctx.Idents.get(this_name));
        } else {
            this_var = clang::VarDecl::Create(m_ctx, method, {}, {}, &m_ctx.Idents.get(this_name), method->getThisType(),
                                              nullptr, clang::SC_None);
            replaceThis(callee->getBody(), clang::DeclRefExpr::Create(m_ctx, {}, {}, this_var, false, clang::SourceLocation(),
                                                                      this_var->getType(), clang::VK_LValue));
        }
    }

    // every parameter becomes a variable of the block, eg. `int __inline_param__3_0 = a;`, which
    // evaluates each argument once, before the body, as the call would
    std::map<int, std::string> caller_params;
    for (unsigned i = 0; i < callee->getNumParams(); ++i) {
        clang::ParmVarDecl* param_decl = callee->getParamDecl(i);
        clang::Expr* arg = call->getArg(i + first_arg);

        std::string arg_text;
        llvm::raw_string_ostream stream(arg_text);
        if (auto* default_arg = llvm::dyn_cast<clang::CXXDefaultArgExpr>(arg))
            default_arg->getExpr()->printPretty(stream, nullptr, m_policy);
        else
            arg->printPretty(stream, nullptr, m_policy);
        stream.flush();

        // a va_list that is an array is passed as a pointer, which va_arg doesn't
        // take, so the argument itself is used
        clang::QualType va_list_type = m_ctx.getBuiltinVaListType();
        if (va_list_type->isArrayType() && m_ctx.hasSameType(param_decl->getOriginalType(), va_list_type)) {
            caller_params[i] = arg_text;
        } else {
            caller_params[i] = "__inline_param__" + index + "_" + std::to_string(i);
            param_decl->getType().print(OS, policy, caller_params[i]);
            OS << " = " << arg_text << ";\n";
        }
    }

    std::string raw_body;
    llvm::raw_string_ostream stream(raw_body);

    // the body's labels renamed for this site, see LabelMaker
    LabelMaker visitor(m_ctx, m_current_index);
    visitor.TraverseStmt(callee->getBody());

    // the names the body uses are qualified from the global namespace, as it is pasted into
    // another scope, eg. `max(a, b)` of a function of `namespace util` becomes `::util::max(a, b)`
    clang::Stmt* body = callee->getBody();
    if (m_ctx.getLangOpts().CPlusPlus)
        qualifyNonLocalNames(body, m_ctx);
    std::string bind_type;
    if (keeps_result && reference) {
        llvm::raw_string_ostream bind_out(bind_type);
        callee->getReturnType().print(bind_out, policy, "__inline_ref__");
    }
    InlinePrinterHelper helper(m_ctx, keeps_result ? result : "", bind_type, caller_params, policy, m_current_index);
    body->printPretty(stream, &helper, policy);
    stream.flush();

    // the blank lines before and after the printed body are dropped
    size_t first_content = raw_body.find_first_not_of(" \n\r\t");
    size_t last_content = raw_body.find_last_not_of(" \n\r\t");
    if (first_content != std::string::npos && last_content != std::string::npos)
        raw_body = raw_body.substr(first_content, last_content - first_content + 1);
    OS << raw_body << "\n__inline_exit_" << index << ": ;}";
    OS.flush();

    // the lines the inlined code and the statement after it take in the new text, body included,
    // where inline_functions looks for the compile errors of this site
    m_inserted_line = src_mgr.getPresumedLineNumber(stmt->getBeginLoc());
    m_inserted_span = std::count(inlined_code.begin(), inlined_code.end(), '\n') + 2 +
                      src_mgr.getPresumedLineNumber(stmt->getEndLoc()) - m_inserted_line;

    // the lines below the call move by the lines added less those of what is replaced
    const clang::Stmt* replaced = discarded ? stmt : call;
    auto orig_text = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(replaced->getSourceRange()), src_mgr,
                                                 m_ctx.getLangOpts());
    std::string replacement;
    if (discarded) {
        replacement = inlined_code;
    } else {
        if (!keeps_result) {
            replacement = "(void)0";
        } else if (reference) {
            std::string type;
            llvm::raw_string_ostream type_out(type);
            callee->getReturnType().print(type_out, policy);
            replacement = "static_cast<" + type + ">(*" + result + ")";
        } else {
            replacement = result;
        }
    }
    // replaced first, a replacement at where text was inserted takes that text with it
    m_rewriter.ReplaceText(replaced->getSourceRange(), replacement);
    if (!discarded)
        m_rewriter.InsertTextBefore(stmt->getBeginLoc(), inlined_code + "\n");
    std::string added = discarded ? replacement : inlined_code + "\n" + replacement;
    m_net_lines = std::count(added.begin(), added.end(), '\n') - std::count(orig_text.begin(), orig_text.end(), '\n');
    return true;
}

// --- what the driver of the rounds is told ---

bool InlineVisitor::matched() const { return m_matched; }

const std::string& InlineVisitor::skipped() const { return m_skipped; }

size_t InlineVisitor::get_net_change() { return m_net_lines; }

// --- FunctionDeclMapVisitor ----------------------------------------------

// the rewriter and policy are kept for the interface's sake; only the names are used
FunctionDeclMapVisitor::FunctionDeclMapVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_names(ctx) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
}

bool FunctionDeclMapVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody()) return true;
    if (!func->isThisDeclarationADefinition()) return true;

    m_function_decl_map[m_names.getName(func)] = func;
    return true;
}

std::map<std::string, clang::FunctionDecl*>& FunctionDeclMapVisitor::get_function_decl_map()  {
    return m_function_decl_map;
}

// --- inline_functions ----------------------------------------------

// Sites are inlined in rounds, each from one parse and checked by one compile. A
// round takes the sites whose callee no site left is inlined into, so that a body is
// inlined once what is inlined into it is, and one site of each line. When the round
// doesn't compile, its halves are inlined on their own. eg. with the oracle
//
//     main total 38        total square 28        total at 27
//
// the first round inlines `square` and `at` into `total` (nothing is inlined into them), and
// the second inlines the resulting `total` into `main`, so that main gets square's and at's
// bodies too, as LLVM's bottom-up inliner did.
InlineResult inline_functions(const std::string& source, const std::string& filename,
                              const std::vector<InlineEntry>& entries, std::vector<std::string>& args) {
    addPipelineFlags(args);

    std::vector<size_t> call_sites;
    for (const auto& entry : entries)
        call_sites.emplace_back(entry.line);

    // finish(i, message) reports an entry done
    std::string result = source;
    std::vector<bool> succeeded(entries.size(), false);
    std::vector<bool> done(entries.size(), false);
    auto finish = [&](size_t i, const std::string& message) {
        done[i] = true;
        if (!message.empty())
            llvm::errs() << "invisible: " << message << "\n";
        verbose() << "done with oracle entry " << i << "\n";
    };

    // a site inlined in the round being checked, with what is needed to move the other lines
    struct Applied {
        size_t index;
        size_t line;
        size_t net;
        size_t inserted_line;
        size_t inserted_span;
    };
    // inlines the sites of @p batch from one parse; keeps the result when it compiles, else leaves out
    // the sites the errors are in and tries the rest, or splits the batch in halves
    std::function<void(const std::vector<size_t>&)> apply = [&](const std::vector<size_t>& batch) {
        clang::Rewriter rewriter;
        std::vector<Applied> matched;
        std::string next = runInMemory(result, filename, args, [&](clang::ASTContext& ctx, clang::Sema&) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            FunctionDeclMapVisitor decls(ctx, rewriter);
            decls.TraverseDecl(ctx.getTranslationUnitDecl());
            std::map<const clang::Stmt*, clang::VarDecl*> this_vars;
            CallIndex index = buildCallIndex(ctx);
            for (size_t i : batch) {
                const InlineEntry& entry = entries[i];
                InlineVisitor visitor(ctx, entry.caller, entry.callee, call_sites, i, rewriter, decls.get_function_decl_map(),
                                      this_vars);
                visitor.run(index);
                std::string at = "'" + entry.callee + "' in '" + entry.caller + "' at line " + std::to_string(call_sites[i]);
                if (!visitor.matched()) {
                    finish(i, "warning: no call from '" + entry.caller + "' to '" + entry.callee + "' at line " +
                                  std::to_string(call_sites[i]));
                } else if (!visitor.skipped().empty()) {
                    finish(i, "skipped " + at + ": " + visitor.skipped());
                } else {
                    matched.push_back({i, call_sites[i], visitor.get_net_change(), visitor.m_inserted_line, visitor.m_inserted_span});
                }
            }
        }, rewriter);
        if (matched.empty())
            return;
        if (isCompilable(next, filename, args)) {
            result = std::move(next);
            // the lines below a site move by what it added
            std::vector<size_t> moved = call_sites;
            for (const Applied& site : matched)
                for (size_t i = 0; i < entries.size(); ++i)
                    if (!done[i] && call_sites[i] >= site.line)
                        moved[i] += site.net;
            call_sites = std::move(moved);
            for (const Applied& site : matched) {
                succeeded[site.index] = true;
                finish(site.index, "");
            }
            return;
        }
        if (matched.size() == 1) {
            size_t i = matched[0].index;
            verbose() << lastCompileFailure().diagnostics;
            finish(i, "warning: inlining '" + entries[i].callee + "' into '" + entries[i].caller + "' at line " +
                          std::to_string(call_sites[i]) + " produced code that does not compile, skipped it");
            return;
        }
        // the sites the errors are in are left out, or when an error isn't in any, half of them
        static const llvm::Regex error("^[^ ]*:([0-9]+):[0-9]+: error:");
        llvm::SmallVector<llvm::StringRef, 16> diagnostics;
        llvm::StringRef(lastCompileFailure().diagnostics).split(diagnostics, '\n');
        std::set<size_t> failed;
        bool unknown = false;
        for (llvm::StringRef diagnostic : diagnostics) {
            llvm::SmallVector<llvm::StringRef, 2> match;
            size_t line = 0;
            if (!error.match(diagnostic, &match) || match[1].getAsInteger(10, line)) continue;
            bool found = false;
            for (const Applied& site : matched) {
                size_t begin = site.inserted_line;
                for (const Applied& other : matched)
                    if (&other != &site && site.inserted_line >= other.line)
                        begin += other.net;
                if (begin <= line && line < begin + site.inserted_span) {
                    failed.insert(site.index);
                    found = true;
                }
            }
            unknown = unknown || !found;
        }
        if (!failed.empty() && !unknown) {
            std::vector<size_t> rest;
            for (const Applied& site : matched) {
                if (!failed.count(site.index)) {
                    rest.push_back(site.index);
                    continue;
                }
                finish(site.index, "warning: inlining '" + entries[site.index].callee + "' into '" + entries[site.index].caller +
                                       "' at line " + std::to_string(call_sites[site.index]) +
                                       " produced code that does not compile, skipped it");
            }
            if (!rest.empty()) apply(rest);
            return;
        }
        std::vector<size_t> first, second;
        for (size_t k = 0; k < matched.size(); ++k)
            (k < matched.size() / 2 ? first : second).push_back(matched[k].index);
        apply(first);
        apply(second);
    };

    // the rounds: until every entry is done. LLVM inlines bottom up, so a round takes the entries
    // whose callee has nothing left to inline into it: `total square 28` before `main total 38`,
    // so that main gets the body of total that already has square's body in it.
    for (;;) {
        // the entries left, and how many of them inline into each function, by its name
        std::map<std::string, size_t> callers;
        std::vector<size_t> pending;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (done[i])
                continue;
            pending.push_back(i);
            callers[entries[i].caller]++;
        }
        if (pending.empty())
            break;
        std::vector<size_t> round;
        std::set<size_t> lines;
        for (size_t i : pending) {
            const InlineEntry& entry = entries[i];
            // the entry waits while anything is left to inline into its callee. A recursive entry,
            // `f f 12`, is itself counted in callers["f"], and only waits for the others
            auto into = callers.find(entry.callee);
            if (into != callers.end() && into->second > (entry.caller == entry.callee ? 1 : 0))
                continue;
            // one entry per line: a site is looked up by its line, so two entries of one line, eg.
            // `main f 5` twice, would both find the same call, their edits would overlap, and a
            // compile error on that line couldn't be told to either. The others wait for the next
            // round, where the inlined call is gone
            if (!lines.insert(call_sites[i]).second)
                continue;
            round.push_back(i);
        }
        // every entry left waits for another, eg. `f g 3` and `g f 9`, or `f f 3` and `f f 4`,
        // and the round would be empty forever: the first entry left is taken alone, which may
        // free the others. It may not be in the cycle itself, eg. `main f 40` that only waits for
        // it, which then goes a little out of LLVM's order, but each round finishes an entry
        if (round.empty())
            round.push_back(pending.front());
        apply(round);
    }

    return {result, succeeded};
}

}  // namespace invisible
