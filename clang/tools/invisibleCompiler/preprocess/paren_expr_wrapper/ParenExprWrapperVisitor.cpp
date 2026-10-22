#include "preprocess/paren_expr_wrapper/ParenExprWrapperVisitor.h"
#include "helpers.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/JSON.h"
#include "clang/AST/TemplateBase.h"

#include <string>
#include <queue>
#include <stack>

namespace invisible {

// --- ParenExprWrapperVisitor ----------------------------------------------

ParenExprWrapperVisitor::ParenExprWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) {}

// Parenthesizes a comma operator that isn't already, eg. `x = a(), b();` => `x = (a(), b());`.
// Later passes edit operands as text, eg. replace `b()` with a temporary, or put text before or
// after an operand. The parentheses keep the comma from grouping with the text around it, eg. in
// a for statement's increment.
bool ParenExprWrapperVisitor::VisitBinaryOperator(clang::BinaryOperator* bin_op) {
    if(bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Comma) {
        if (isWrappedWithParenExpr(bin_op)) return true;

        m_rewriter.InsertTextBefore(bin_op->getSourceRange().getBegin(), "(");
        m_rewriter.InsertTextAfterToken(bin_op->getSourceRange().getEnd(), ")");
    }
    return true;
}

// whether the comma operator is directly in parentheses already, eg. `(a, b)`, so that the pass
// doesn't add a pair on every run
bool ParenExprWrapperVisitor::isWrappedWithParenExpr(const clang::BinaryOperator* binary_op) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*binary_op);
    const auto& parent = m_ctx.getParents(current)[0];
    if(parent.get<clang::ParenExpr>() != nullptr) return true;
    else return false;
}

}  // namespace invisible
