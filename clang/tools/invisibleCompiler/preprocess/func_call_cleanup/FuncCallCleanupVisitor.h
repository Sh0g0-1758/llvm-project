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
    FuncCallCleanupVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, size_t& current_index);

    bool VisitCallExpr(clang::CallExpr* call);
    bool did_cleanup();
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    void hoist_call_at_nearest_compound_statement(clang::CallExpr* call);
  private:
    clang::ASTContext& m_ctx;
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
