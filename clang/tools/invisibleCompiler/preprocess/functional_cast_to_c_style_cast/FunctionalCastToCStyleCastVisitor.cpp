#include "preprocess/functional_cast_to_c_style_cast/FunctionalCastToCStyleCastVisitor.h"
#include "helpers.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"

namespace invisible {

// --- FunctionalCastToCStyleCastVisitor --------------------------------------

FunctionalCastToCStyleCastVisitor::FunctionalCastToCStyleCastVisitor(clang::ASTContext& ctx, clang::Sema& sema)
    : m_ctx(ctx), m_sema(sema), m_policy(codePolicy(ctx)) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
}

// Replaces the casts among the children of @p stmt once they are traversed. A cast can only be
// replaced through its parent's pointer to it, so the parent does it. An InitListExpr has a second
// list of children, its syntactic form, which is the one the printer prints, so it is done there too.
bool FunctionalCastToCStyleCastVisitor::TraverseStmt(clang::Stmt* stmt) {
    if (!stmt) return true;
    if (!clang::RecursiveASTVisitor<FunctionalCastToCStyleCastVisitor>::TraverseStmt(stmt)) return false;
    replaceInChildren(stmt);
    if (auto* ile = llvm::dyn_cast<clang::InitListExpr>(stmt))
        replaceInChildren(ile->getSyntacticForm());
    return true;
}

// Whether `T(x)` can be printed with @p type as T: a name of one word, also qualified or with
// template arguments, eg. `size_t` or `std::vector<int>`, not `unsigned long`, `const T` or `T*`.
// Both the type as written and its canonical form are checked, as a copy prints the canonical
// one: `size_type(n)` parses, but `unsigned long(n)` doesn't.
bool FunctionalCastToCStyleCastVisitor::isSimpleTypeName(clang::QualType type) {
    std::string name = type.getAsString(m_policy) + "," + type.getCanonicalType().getAsString(m_policy);
    // a space, `*`, `&` or `[` outside `<...>` and `(...)` means more than a name
    int depth = 0;
    for (char c : name) {
        if (c == '<' || c == '(') depth++;
        else if (c == '>' || c == ')') depth--;
        else if (depth == 0 && (c == ' ' || c == '*' || c == '&' || c == '['))
            return false;
        else if (depth == 0 && c == ',')
            depth = 0;
    }
    return true;
}

// @p operand, parenthesized when it is a binary or conditional operator: `T(a - b)` must become
// `(T)(a - b)`, as `(T)a - b` only casts `a`
clang::Expr* FunctionalCastToCStyleCastVisitor::parenthesized(clang::Expr* operand) {
    clang::Expr* inner = operand->IgnoreImpCasts();
    const auto* call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(inner);
    if (!llvm::isa<clang::BinaryOperator, clang::AbstractConditionalOperator>(inner) && !(call && call->isInfixBinaryOp()))
        return operand;
    return new (m_ctx) clang::ParenExpr(operand->getBeginLoc(), operand->getEndLoc(), operand);
}

// decl-before-def prints instantiations, where a function style cast `T(x)` names the type T was
// given, eg. `unsigned long(x)` for `T = unsigned long`, which doesn't parse. Such casts become C
// style casts, which take any type:
//
//     T(x)  =>  (T)(x)        T{x}  =>  (T)(x)        T{}  =>  (T)0        T()  =>  ((T)0)
//
// A cast is left when its type prints as a simple name, depends on a template or is deduced, eg.
// `auto(x)`.
void FunctionalCastToCStyleCastVisitor::replaceInChildren(clang::Stmt* stmt) {
    if (!stmt) return;
    for (clang::Stmt*& child : stmt->children()) {
        // `T()` of a scalar, its value initialization, is a cast of 0
        if (auto* init = llvm::dyn_cast_or_null<clang::CXXScalarValueInitExpr>(child);
            init && !init->getType()->isDependentType() && !isSimpleTypeName(init->getType())) {
            clang::Expr* zero = clang::IntegerLiteral::Create(m_ctx, llvm::APInt(m_ctx.getIntWidth(m_ctx.IntTy), 0), m_ctx.IntTy,
                                                              init->getBeginLoc());
            clang::ExprResult result = m_sema.BuildCStyleCastExpr(init->getBeginLoc(), init->getTypeSourceInfo(), init->getEndLoc(), zero);
            if (!result.isInvalid()) child = new (m_ctx) clang::ParenExpr(init->getBeginLoc(), init->getEndLoc(), result.get());
            continue;
        }
        auto* cast = llvm::dyn_cast_or_null<clang::CXXFunctionalCastExpr>(child);
        if (!cast) continue;
        clang::QualType type = cast->getType();
        if (type->isDependentType() || type->getContainedDeducedType()) continue;
        if (isSimpleTypeName(type)) continue;

        if (cast->isListInitialization()) {
            // `T{x}` becomes `(T)(x)` and `T{}` becomes `(T)0`. Braces of several values, eg. of
            // an aggregate `P{1, 2}`, have no cast form and are left.
            auto* ile = llvm::dyn_cast<clang::InitListExpr>(cast->getSubExpr());
            if (ile && !ile->isSemanticForm()) ile = ile->getSemanticForm();
            if (!ile || ile->getNumInits() > 1) continue;
            clang::Expr* operand = ile->getNumInits() == 1 ? ile->getInit(0) :
                clang::IntegerLiteral::Create(m_ctx, llvm::APInt(m_ctx.getIntWidth(m_ctx.IntTy), 0), m_ctx.IntTy, cast->getBeginLoc());
            operand = parenthesized(operand);
            clang::ExprResult result = m_sema.BuildCStyleCastExpr(cast->getBeginLoc(), cast->getTypeInfoAsWritten(), cast->getEndLoc(), operand);
            if (!result.isInvalid()) child = result.get();
            continue;
        }

        // `T(x)`: the same cast, of the same kind and base path, spelled the C way
        clang::CXXCastPath path(cast->path_begin(), cast->path_end());
        child = clang::CStyleCastExpr::Create(
            m_ctx, type, cast->getValueKind(), cast->getCastKind(),
            parenthesized(cast->getSubExpr()), &path,
            cast->hasStoredFPFeatures() ? cast->getFPFeatures() : clang::FPOptionsOverride(),
            cast->getTypeInfoAsWritten(), cast->getLParenLoc(), cast->getRParenLoc());
    }
}

}  // namespace invisible
