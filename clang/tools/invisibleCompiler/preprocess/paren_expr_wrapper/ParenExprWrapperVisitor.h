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

// --- ParenExprWrapperVisitor --------------------------------------

// paren-expr-wrapper: parenthesizes every comma operator, so that what a later pass moves out of
// an operand doesn't change how it groups, see docs/DESIGN.md section 5.8.
class ParenExprWrapperVisitor : public LaterPassVisitor<ParenExprWrapperVisitor> {
  public:
    ParenExprWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    // `a(), b()` => `(a(), b())`
    bool VisitBinaryOperator(clang::BinaryOperator* bin_op);
    // whether @p binary_op is directly in parentheses already
    bool isWrappedWithParenExpr(const clang::BinaryOperator* binary_op);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
};

}  // namespace invisible
