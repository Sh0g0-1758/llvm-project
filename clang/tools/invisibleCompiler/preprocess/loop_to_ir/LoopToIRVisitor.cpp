#include "preprocess/loop_to_ir/LoopToIRVisitor.h"
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

// ─── LoopToIRVisitor ──────────────────────────────────────────────

LoopToIRVisitor::LoopToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool LoopToIRVisitor::VisitForStmt(clang::ForStmt* for_stmt) {
    /**
     * `for (init, cond, inc) body`
     * is transformed into
     * for (init;;) {
     *   if (!(cond)) { break; }
     *   body [stripped of surrounding braces]
     *   expr
     * }
     */
    bool hasCond = for_stmt->getCond() != nullptr;
    bool hasInc  = for_stmt->getInc() != nullptr;
    if(!hasCond && !hasInc) return true;
    if(hasCond && !exprHasFuncCall(for_stmt->getCond()) &&
      (hasInc && !exprHasFuncCall(for_stmt->getInc()))) return true;

    std::string cond;
    llvm::raw_string_ostream cond_out(cond);
    if(hasCond) for_stmt->getCond()->printPretty(cond_out, nullptr, m_policy);
    cond_out.flush();

    std::string inc;
    llvm::raw_string_ostream inc_out(inc);
    if(hasInc) {
        for_stmt->getInc()->printPretty(inc_out, nullptr, m_policy);
        inc_out << ";";
    }
    inc_out.flush();

    if (hasCond) m_rewriter.ReplaceText(for_stmt->getCond()->getSourceRange(), "");
    if (hasInc) m_rewriter.ReplaceText(for_stmt->getInc()->getSourceRange(), "");

    std::string cond_wrapped_in_if;
    llvm::raw_string_ostream cond_wrapped_out(cond_wrapped_in_if);
    if(hasCond) cond_wrapped_out << "if (!(" << cond << ")) {\nbreak;\n}\n";
    cond_wrapped_out.flush();
    
    if(hasCond) m_rewriter.InsertTextBefore(for_stmt->getBody()->getSourceRange().getBegin().getLocWithOffset(1), cond_wrapped_in_if);
    if(hasInc) m_rewriter.InsertTextBefore(for_stmt->getBody()->getSourceRange().getEnd(), inc);

    return true;
}

bool LoopToIRVisitor::VisitWhileStmt(clang::WhileStmt* while_stmt) {
    /**
     * {if cond has a function call}
     * `while (cond) body`
     * is transformed into
     * while (1) {
     *   if (!(cond)) { break; }
     *   body [stripped of surrounding braces]
     * }
     * 
     * {if init has a function call}
     * `while (T v = init) body`
     * is transformed into
     *   while (1) {
     *     T v = init;
     *     if (!(v)) { break; }
     *     body [stripped of surrounding braces]
     *   }
     * 
     */
    auto cond_var = while_stmt->getConditionVariable();
    clang::Expr* trigger = cond_var ? cond_var->getInit() : while_stmt->getCond();
    if (!trigger || !exprHasFuncCall(trigger)) return true;

    std::string cond;
    llvm::raw_string_ostream cond_out(cond);
    while_stmt->getCond()->printPretty(cond_out, nullptr, m_policy);
    cond_out.flush();

    std::string init{};
    if (auto decl_stmt = while_stmt->getConditionVariableDeclStmt()) {
        llvm::raw_string_ostream init_out(init);
        decl_stmt->printPretty(init_out, nullptr, m_policy);
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), "1");
    } else {
        m_rewriter.ReplaceText(while_stmt->getCond()->getSourceRange(), "1");
    }

    std::string cond_wrapped_in_if;
    llvm::raw_string_ostream cond_wrapped_out(cond_wrapped_in_if);
    cond_wrapped_out << "if (!(" << cond << ")) {\nbreak;\n}\n";
    cond_wrapped_out.flush();

    m_rewriter.InsertTextBefore(while_stmt->getBody()->getSourceRange().getBegin().getLocWithOffset(1), init + cond_wrapped_in_if);

    return true;
}

bool LoopToIRVisitor::VisitDoStmt(clang::DoStmt* do_stmt) {
    /**
     * `do body while (cond);`
     * is transformed into
     * do {
     *   body [stripped of surrounding braces]
     *   if (!(cond)) { break; }
     * } while(1);
     */
    if (!exprHasFuncCall(do_stmt->getCond())) return true;

    std::string cond;
    llvm::raw_string_ostream cond_out(cond);
    do_stmt->getCond()->printPretty(cond_out, nullptr, m_policy);
    cond_out.flush();

    m_rewriter.ReplaceText(do_stmt->getCond()->getSourceRange(), "1");

    std::string cond_wrapped_in_if;
    llvm::raw_string_ostream cond_wrapped_out(cond_wrapped_in_if);
    cond_wrapped_out << "if (!(" << cond << ")) {\nbreak;\n}\n";
    cond_wrapped_out.flush();

    m_rewriter.InsertTextBefore(do_stmt->getBody()->getSourceRange().getEnd(), cond_wrapped_in_if);

    return true;
}

bool LoopToIRVisitor::exprHasFuncCall(clang::Expr* expr) {
    expr = expr->IgnoreParenImpCasts();
    if(llvm::dyn_cast<clang::CallExpr>(expr)) return true;
    clang::Stmt* stmt = expr;
    std::queue<clang::Stmt*> children;
    for (const clang::Stmt* child : stmt->children()) {
        if (child == nullptr) continue;
        children.push(const_cast<clang::Stmt*>(child));
    }
    bool stmtIsFuncCall = false;
    while(!children.empty()) {
        clang::Stmt* child = children.front();
        if(clang::Expr* expr = llvm::dyn_cast<clang::Expr>(child))
            if(llvm::dyn_cast<clang::CallExpr>(expr->IgnoreParenImpCasts()))
                stmtIsFuncCall = true;
        if(!stmtIsFuncCall) {
            for (const clang::Stmt* grandChild : child->children()) {
                if (grandChild == nullptr) continue;
                children.push(const_cast<clang::Stmt*>(grandChild));
            }
        } else break;
        children.pop();
    }
    return stmtIsFuncCall;
}

}  // namespace invisible
