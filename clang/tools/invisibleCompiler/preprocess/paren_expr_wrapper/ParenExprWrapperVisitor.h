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

// ─── ParenExprWrapperVisitor ──────────────────────────────────────

/// Wraps binary operator (,) with a paranthesis
class ParenExprWrapperVisitor : public clang::RecursiveASTVisitor<ParenExprWrapperVisitor> {
  public:
    bool TraverseDecl(clang::Decl* decl) {
        return isLeftAsWritten(decl) || clang::RecursiveASTVisitor<ParenExprWrapperVisitor>::TraverseDecl(decl);
    }
    ParenExprWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitBinaryOperator(clang::BinaryOperator* bin_op);
    bool isWrappedWithParenExpr(const clang::BinaryOperator* binary_op);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
};

}  // namespace invisible
