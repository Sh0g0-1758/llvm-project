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

// ─── MarkSwitchCaseVisitor ──────────────────────────────────────

/// Marks the beginning of every case statement with a semi-colon so
/// that any function call present at the start of the set of statements
/// for case is hoisted as part of the case and not above it. 
class MarkSwitchCaseVisitor : public clang::RecursiveASTVisitor<MarkSwitchCaseVisitor> {
  public:
    bool TraverseDecl(clang::Decl* decl) {
        return isLeftAsWritten(decl) || clang::RecursiveASTVisitor<MarkSwitchCaseVisitor>::TraverseDecl(decl);
    }
    MarkSwitchCaseVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitCaseStmt(clang::CaseStmt* case_stmt);
    bool VisitDefaultStmt(clang::DefaultStmt* default_stmt);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
};

}  // namespace invisible
