#include "preprocess/mark_switch_case/MarkSwitchCaseVisitor.h"
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

// ─── MarkSwitchCaseVisitor ──────────────────────────────────────────────

MarkSwitchCaseVisitor::MarkSwitchCaseVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
}

bool MarkSwitchCaseVisitor::VisitCaseStmt(clang::CaseStmt* case_stmt) {
    m_rewriter.InsertTextBefore(case_stmt->getSubStmt()->getSourceRange().getBegin(), ";\n");
    return true;
}

bool MarkSwitchCaseVisitor::VisitDefaultStmt(clang::DefaultStmt* default_stmt) {
    m_rewriter.InsertTextBefore(default_stmt->getSubStmt()->getSourceRange().getBegin(), ";\n");
    return true;
}

}  // namespace invisible
