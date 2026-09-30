#pragma once

#include "helpers.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Sema/Sema.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/FoldingSet.h"
#include "clang/AST/ASTTypeTraits.h"

#include <map>
#include <unordered_map>
#include <deque>
#include <string>
#include <vector>
#include <cassert>

namespace invisible {

// ─── FuncCallCleanupVisitor ────────────────────────────────────────

/// Hoists nested function calls out of argument positions into
/// temporary variables so each call is a standalone statement.
///
/// On each pass, all innermost nested calls (those whose arguments
/// contain no further nested calls) are hoisted in one traversal.
/// This avoids the O(N) re-parse overhead of hoisting one call at a
/// time.
class FuncCallCleanupVisitor : public clang::RecursiveASTVisitor<FuncCallCleanupVisitor> {
  public:
    bool TraverseDecl(clang::Decl* decl) {
        return isLeftAsWritten(decl) || clang::RecursiveASTVisitor<FuncCallCleanupVisitor>::TraverseDecl(decl);
    }
    FuncCallCleanupVisitor(clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter, size_t& current_index);

    // a call in an unevaluated operand is never made, it only names a type or a
    // property of one: hoisting it would make it, and eg. turn `decltype(f())` of a
    // prvalue into a reference type. These operands are left as written.
    bool TraverseDecltypeTypeLoc(clang::DecltypeTypeLoc, bool) { return true; }
    bool TraverseTypeOfExprTypeLoc(clang::TypeOfExprTypeLoc, bool) { return true; }
    bool TraverseCXXNoexceptExpr(clang::CXXNoexceptExpr*, DataRecursionQueue* = nullptr) { return true; }
    bool TraverseRequiresExpr(clang::RequiresExpr*, DataRecursionQueue* = nullptr) { return true; }
    bool TraverseUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr* expr, DataRecursionQueue* queue = nullptr) {
        // the size of a variable length array is worked out at run time
        return !expr->getTypeOfArgument()->isVariablyModifiedType() ||
               clang::RecursiveASTVisitor<FuncCallCleanupVisitor>::TraverseUnaryExprOrTypeTraitExpr(expr, queue);
    }
    bool TraverseCXXTypeidExpr(clang::CXXTypeidExpr* expr, DataRecursionQueue* queue = nullptr) {
        // the operand of typeid is evaluated only for a glvalue of a polymorphic class
        return !expr->isPotentiallyEvaluated() ||
               clang::RecursiveASTVisitor<FuncCallCleanupVisitor>::TraverseCXXTypeidExpr(expr, queue);
    }

    bool VisitCallExpr(clang::CallExpr* call);
    bool did_cleanup();
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    void hoist_call_at_nearest_compound_statement(clang::CallExpr* call);
  private:
    clang::ASTContext& m_ctx;
    clang::Sema& m_sema;
    clang::Rewriter& m_rewriter;
    clang::SourceLocation m_last_update_loc;
    clang::SourceRange m_last_call_loc;

    clang::SourceLocation findInsertionLoc(const clang::CallExpr* call, const clang::Stmt** statement);
    bool isTopLevelCall(const clang::CallExpr* call);
    bool m_is_uninstantiated_template;
    size_t& m_current_index;
    clang::PrintingPolicy m_policy;
    bool isTransparentParent(const clang::Stmt* s, bool isExplicitCoversionOperator);
    clang::DynTypedNode getSemanticParent(const clang::Stmt* node, bool isExplicitCoversionOperator);
};

}  // namespace invisible
