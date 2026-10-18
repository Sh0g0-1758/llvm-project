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

namespace {

// Past the end of @p stmt. clang's range of a statement ending with an expression, a jump or a
// do-while stops before its `;`, eg. at the `)` of `case 0: f();`
clang::SourceLocation endOf(const clang::Stmt* stmt, const clang::ASTContext& ctx) {
    const clang::Stmt* last = stmt;
    for (;;) {
        if (const auto* label = llvm::dyn_cast<clang::SwitchCase>(last)) last = label->getSubStmt();
        else if (const auto* label = llvm::dyn_cast<clang::LabelStmt>(last)) last = label->getSubStmt();
        else if (const auto* inner = llvm::dyn_cast<clang::SwitchStmt>(last)) last = inner->getBody();
        else break;
    }
    const clang::SourceManager& src_mgr = ctx.getSourceManager();
    if (llvm::isa<clang::Expr, clang::ReturnStmt, clang::BreakStmt, clang::ContinueStmt, clang::GotoStmt, clang::DoStmt>(last))
        if (clang::SourceLocation semi =
                clang::Lexer::findLocationAfterToken(stmt->getEndLoc(), clang::tok::semi, src_mgr, ctx.getLangOpts(), false);
            semi.isValid())
            return semi;
    return clang::Lexer::getLocForEndOfToken(stmt->getEndLoc(), 0, src_mgr, ctx.getLangOpts());
}

}  // namespace

// ─── CompoundStmtWrapperVisitor ──────────────────────────────────────────────

CompoundStmtWrapperVisitor::CompoundStmtWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) {}

// Wraps a for statement in a block, eg.
//
//     for (int i = 0, n = size(); i < n; ++i) f(i);   =>   {
//                                                            for (int i = 0, n = size(); i < n; ++i) f(i);
//                                                          }
//
// loop-to-ir and comma-init move the init statement's declarations before the loop, and
// hoist-calls puts temporaries there; the block keeps them scoped to the loop, as in the source.
// An empty body, `for (;;);`, becomes `{}`, a block that statements can be added to.
bool CompoundStmtWrapperVisitor::VisitForStmt(clang::ForStmt* for_stmt) {
    m_rewriter.InsertTextBefore(for_stmt->getSourceRange().getBegin(), "{\n");
    m_rewriter.InsertTextBefore(for_stmt->getSourceRange().getEnd().getLocWithOffset(1), "\n}");
    if(llvm::dyn_cast<clang::NullStmt>(for_stmt->getBody())) m_rewriter.ReplaceText(for_stmt->getBody()->getSourceRange(), "{}");
    return true;
}

// An empty while body, `while (poll());`, becomes `{}`, so that loop-to-ir can put the
// condition's `if (!(poll())) break;` in it.
bool CompoundStmtWrapperVisitor::VisitWhileStmt(clang::WhileStmt* while_stmt) {
    if(llvm::dyn_cast<clang::NullStmt>(while_stmt->getBody())) m_rewriter.ReplaceText(while_stmt->getBody()->getSourceRange(), "{}");
    return true;
}

// An empty do body, `do ; while (poll());`, becomes `{}`, as for while.
bool CompoundStmtWrapperVisitor::VisitDoStmt(clang::DoStmt* do_stmt) {
    if(llvm::dyn_cast<clang::NullStmt>(do_stmt->getBody())) m_rewriter.ReplaceText(do_stmt->getBody()->getSourceRange(), "{}");
    return true;
}

// Wraps an if statement in a block, eg. `if (auto p = find()) use(p);` => `{ if (...) use(p); }`:
// loop-to-ir moves the condition's declaration, `auto p = find();`, before the if, and the
// block keeps it out of the scope of what follows, where another `p` may be declared.
bool CompoundStmtWrapperVisitor::VisitIfStmt(clang::IfStmt* if_stmt) {
    m_rewriter.InsertTextBefore(if_stmt->getSourceRange().getBegin(), "{\n");
    m_rewriter.InsertTextBefore(if_stmt->getSourceRange().getEnd().getLocWithOffset(1), "\n}");
    return true;
}

// Wraps a switch statement in a block, for the declaration of its condition or init statement,
// eg. `switch (int c = next()) {...}`, as for if. A body that isn't a block becomes one, eg.
// `switch (x) case 0: default: f();` gives `switch (x) { case 0: default: f(); }`, so that
// mark-switch-case finds every label in a block.
bool CompoundStmtWrapperVisitor::VisitSwitchStmt(clang::SwitchStmt* switch_stmt) {
    clang::SourceLocation end = endOf(switch_stmt->getBody(), m_ctx);
    m_rewriter.InsertTextBefore(switch_stmt->getBeginLoc(), "{\n");
    if (!llvm::isa<clang::CompoundStmt>(switch_stmt->getBody())) {
        m_rewriter.InsertTextBefore(switch_stmt->getBody()->getBeginLoc(), "{\n");
        m_rewriter.InsertTextBefore(end, "\n}");
    }
    m_rewriter.InsertTextBefore(end, "\n}");
    return true;
}

}  // namespace invisible
