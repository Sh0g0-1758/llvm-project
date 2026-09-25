#pragma once

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

// ─── CompoundStmtWrapperVisitor ──────────────────────────────────────

/// Wraps statements which may have a declStmt (for/if/switch) with a compound stmt
/// This helps provide a valid space to hoist a comma separated declarator list
class CompoundStmtWrapperVisitor : public clang::RecursiveASTVisitor<CompoundStmtWrapperVisitor> {
  public:
    CompoundStmtWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitForStmt(clang::ForStmt* for_stmt);
    bool VisitIfStmt(clang::IfStmt* if_stmt);
    bool VisitSwitchStmt(clang::SwitchStmt* switch_stmt);
    bool VisitWhileStmt(clang::WhileStmt* while_stmt);
    bool VisitDoStmt(clang::DoStmt* do_stmt);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
};

}  // namespace invisible
