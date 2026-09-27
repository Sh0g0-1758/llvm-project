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

// ─── ShortCircuitOperatorToIRVisitor ──────────────────────────────────────

/// Writes short-circuit operations in their IR form at the source code level
class ShortCircuitOperatorToIRVisitor : public clang::RecursiveASTVisitor<ShortCircuitOperatorToIRVisitor> {
  public:
    bool TraverseDecl(clang::Decl* decl) {
        return isLeftAsWritten(decl) || clang::RecursiveASTVisitor<ShortCircuitOperatorToIRVisitor>::TraverseDecl(decl);
    }
    ShortCircuitOperatorToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, size_t& current_index);
    bool VisitBinaryOperator(clang::BinaryOperator* binary_op);
    bool VisitConditionalOperator(clang::ConditionalOperator* conditional_op);
    clang::SourceLocation findInsertionLoc(const clang::BinaryOperator* binary_op);
    clang::SourceLocation findInsertionLoc(const clang::ConditionalOperator* conditional_op);
    bool exprHasFuncCall(clang::Expr* expr);
    bool didConvertToIR();
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    size_t& m_current_index;
    size_t m_last_update_loc;
};

}  // namespace invisible
