#include "preprocess/compound_stmt_wrapper/CompoundStmtWrapperVisitor.h"
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

// ─── CompoundStmtWrapperVisitor ──────────────────────────────────────────────

CompoundStmtWrapperVisitor::CompoundStmtWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool CompoundStmtWrapperVisitor::VisitForStmt(clang::ForStmt* for_stmt) {
    m_rewriter.InsertTextBefore(for_stmt->getSourceRange().getBegin(), "{\n");
    m_rewriter.InsertTextBefore(for_stmt->getSourceRange().getEnd().getLocWithOffset(1), "\n}");
    if(llvm::dyn_cast<clang::NullStmt>(for_stmt->getBody())) m_rewriter.ReplaceText(for_stmt->getBody()->getSourceRange(), "{}");
    return true;
}

bool CompoundStmtWrapperVisitor::VisitWhileStmt(clang::WhileStmt* while_stmt) {
    if(llvm::dyn_cast<clang::NullStmt>(while_stmt->getBody())) m_rewriter.ReplaceText(while_stmt->getBody()->getSourceRange(), "{}");
    return true;
}

bool CompoundStmtWrapperVisitor::VisitDoStmt(clang::DoStmt* do_stmt) {
    if(llvm::dyn_cast<clang::NullStmt>(do_stmt->getBody())) m_rewriter.ReplaceText(do_stmt->getBody()->getSourceRange(), "{}");
    return true;
}

bool CompoundStmtWrapperVisitor::VisitIfStmt(clang::IfStmt* if_stmt) {
    m_rewriter.InsertTextBefore(if_stmt->getSourceRange().getBegin(), "{\n");
    m_rewriter.InsertTextBefore(if_stmt->getSourceRange().getEnd().getLocWithOffset(1), "\n}");
    return true;
}

bool CompoundStmtWrapperVisitor::VisitSwitchStmt(clang::SwitchStmt* switch_stmt) {
    m_rewriter.InsertTextBefore(switch_stmt->getSourceRange().getBegin(), "{\n");
    m_rewriter.InsertTextBefore(switch_stmt->getSourceRange().getEnd().getLocWithOffset(1), "\n}");
    return true;
}

}  // namespace invisible
