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

// ─── LoopToIRVisitor ──────────────────────────────────────

/// Writes Loops in their IR form at the source code level
class LoopToIRVisitor : public clang::RecursiveASTVisitor<LoopToIRVisitor> {
  public:
    bool TraverseDecl(clang::Decl* decl) {
        return isLeftAsWritten(decl) || clang::RecursiveASTVisitor<LoopToIRVisitor>::TraverseDecl(decl);
    }
    LoopToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitForStmt(clang::ForStmt* for_stmt);
    bool VisitWhileStmt(clang::WhileStmt* while_stmt);
    bool VisitDoStmt(clang::DoStmt* do_stmt);
    bool exprHasFuncCall(clang::Expr* expr);
  private:
    void collectContinues(clang::Stmt* stmt, std::vector<clang::ContinueStmt*>& continues);
    std::string jumpContinuesToLabel(clang::Stmt* body);
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    size_t m_label_index = 0;
};

}  // namespace invisible
