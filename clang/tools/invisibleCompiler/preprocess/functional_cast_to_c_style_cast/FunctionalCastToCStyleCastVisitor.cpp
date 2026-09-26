#include "preprocess/functional_cast_to_c_style_cast/FunctionalCastToCStyleCastVisitor.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"

namespace invisible {

// ─── FunctionalCastToCStyleCastVisitor ──────────────────────────────────────

FunctionalCastToCStyleCastVisitor::FunctionalCastToCStyleCastVisitor(clang::ASTContext& ctx, clang::Sema& sema)
    : m_ctx(ctx), m_sema(sema), m_policy(ctx.getLangOpts()) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
}

// casts are replaced through their parent's child slot, after the children have
// been traversed. The syntactic form of an InitListExpr has its own array of
// children and is the one that gets printed.
bool FunctionalCastToCStyleCastVisitor::TraverseStmt(clang::Stmt* stmt) {
    if (!stmt) return true;
    if (!clang::RecursiveASTVisitor<FunctionalCastToCStyleCastVisitor>::TraverseStmt(stmt)) return false;
    replaceInChildren(stmt);
    if (auto* ile = llvm::dyn_cast<clang::InitListExpr>(stmt))
        replaceInChildren(ile->getSyntacticForm());
    return true;
}

// a simple type name has no top level spaces or declarator symbols, unlike
// `unsigned long`, `const T` or `T*`.
bool FunctionalCastToCStyleCastVisitor::isSimpleTypeName(clang::QualType type) {
    std::string name = type.getAsString(m_policy);
    int depth = 0;
    for (char c : name) {
        if (c == '<' || c == '(') depth++;
        else if (c == '>' || c == ')') depth--;
        else if (depth == 0 && (c == ' ' || c == '*' || c == '&' || c == '['))
            return false;
    }
    return true;
}

void FunctionalCastToCStyleCastVisitor::replaceInChildren(clang::Stmt* stmt) {
    if (!stmt) return;
    for (clang::Stmt*& child : stmt->children()) {
        auto* cast = llvm::dyn_cast_or_null<clang::CXXFunctionalCastExpr>(child);
        if (!cast) continue;
        clang::QualType type = cast->getType();
        if (type->isDependentType() || type->getContainedDeducedType()) continue;
        if (isSimpleTypeName(type)) continue;

        if (cast->isListInitialization()) {
            // `T{x}` becomes `(T)(x)` and `T{}` becomes `(T)0`.
            auto* ile = llvm::dyn_cast<clang::InitListExpr>(cast->getSubExpr());
            if (ile && !ile->isSemanticForm()) ile = ile->getSemanticForm();
            if (!ile || ile->getNumInits() > 1) continue;
            clang::Expr* operand = ile->getNumInits() == 1 ? ile->getInit(0) :
                clang::IntegerLiteral::Create(m_ctx, llvm::APInt(m_ctx.getIntWidth(m_ctx.IntTy), 0), m_ctx.IntTy, cast->getBeginLoc());
            clang::ExprResult result = m_sema.BuildCStyleCastExpr(cast->getBeginLoc(), cast->getTypeInfoAsWritten(), cast->getEndLoc(), operand);
            if (!result.isInvalid()) child = result.get();
            continue;
        }

        clang::CXXCastPath path(cast->path_begin(), cast->path_end());
        child = clang::CStyleCastExpr::Create(
            m_ctx, type, cast->getValueKind(), cast->getCastKind(), cast->getSubExpr(), &path,
            cast->hasStoredFPFeatures() ? cast->getFPFeatures() : clang::FPOptionsOverride(),
            cast->getTypeInfoAsWritten(), cast->getLParenLoc(), cast->getRParenLoc());
    }
}

}  // namespace invisible
