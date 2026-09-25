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

// ─── ParenExprWrapperVisitor ──────────────────────────────────────────────

ParenExprWrapperVisitor::ParenExprWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool ParenExprWrapperVisitor::VisitBinaryOperator(clang::BinaryOperator* bin_op) {
    if(bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Comma) {
        if (isWrappedWithParenExpr(bin_op)) return true;

        m_rewriter.InsertTextBefore(bin_op->getSourceRange().getBegin(), "(");
        m_rewriter.InsertTextAfterToken(bin_op->getSourceRange().getEnd(), ")");
    }
    return true;
}

bool ParenExprWrapperVisitor::isWrappedWithParenExpr(const clang::BinaryOperator* binary_op) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*binary_op);
    const auto& parent = m_ctx.getParents(current)[0];
    if(parent.get<clang::ParenExpr>() != nullptr) return true;
    else return false;
}

}  // namespace invisible
