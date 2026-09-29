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
}

bool LoopToIRVisitor::VisitForStmt(clang::ForStmt* for_stmt) {
    /**
     * `for (init, cond, inc) body`
     * is transformed into
     * for (init;;) {
     *   if (!(cond)) { break; }
     *   { body }
     *   expr
     * }
     *
     * {if body has a continue}
     * `for (init, cond, inc) body`
     * is transformed into
     * for (init;;) {
     *   if (!(cond)) { break; }
     *   { body [continue replaced by goto __loop_continue_#id] }
     *   __loop_continue_#id: { expr }
     * }
     */
    bool hasCond = for_stmt->getCond() != nullptr;
    bool hasInc  = for_stmt->getInc() != nullptr;
    if(!(hasCond && exprHasFuncCall(for_stmt->getCond())) &&
       !(hasInc && exprHasFuncCall(for_stmt->getInc()))) return true;

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

    std::string label = hasInc ? jumpContinuesToLabel(for_stmt->getBody()) : "";

    if (hasCond) m_rewriter.ReplaceText(for_stmt->getCond()->getSourceRange(), "");
    if (hasInc) m_rewriter.ReplaceText(for_stmt->getInc()->getSourceRange(), "");

    std::string cond_wrapped_in_if;
    llvm::raw_string_ostream cond_wrapped_out(cond_wrapped_in_if);
    if(hasCond) cond_wrapped_out << "if (!(" << cond << ")) {\nbreak;\n}\n";
    cond_wrapped_out << "{\n";
    cond_wrapped_out.flush();
    // calls hoisted out of expr stay inside the block, the goto can't jump over them
    if(!label.empty()) inc = label + ": {\n" + inc + "\n}\n";

    m_rewriter.InsertTextBefore(for_stmt->getBody()->getSourceRange().getBegin().getLocWithOffset(1), cond_wrapped_in_if);
    m_rewriter.InsertTextBefore(for_stmt->getBody()->getSourceRange().getEnd(), "}\n" + inc);

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

bool LoopToIRVisitor::VisitIfStmt(clang::IfStmt* if_stmt) {
    moveConditionDeclsOut(if_stmt, if_stmt->getInit(), if_stmt->getConditionVariableDeclStmt(), if_stmt->getCond(),
                          if_stmt->getLParenLoc(), if_stmt->getRParenLoc());
    return true;
}

bool LoopToIRVisitor::VisitSwitchStmt(clang::SwitchStmt* switch_stmt) {
    moveConditionDeclsOut(switch_stmt, switch_stmt->getInit(), switch_stmt->getConditionVariableDeclStmt(),
                          switch_stmt->getCond(), switch_stmt->getLParenLoc(), switch_stmt->getRParenLoc());
    return true;
}

void LoopToIRVisitor::moveConditionDeclsOut(clang::Stmt* stmt, clang::Stmt* init, clang::DeclStmt* cond_decl,
                                            clang::Expr* cond, clang::SourceLocation lparen, clang::SourceLocation rparen) {
    /**
     * {if the init statement, the condition variable or the condition has a function call}
     * `if (init; T v = e) then else other`
     * is transformed into
     *   {
     *     init;
     *     T v = e;
     *     if (v) then else other
     *   }
     * and likewise for a switch. The calls can then be hoisted before the statement,
     * which the variables they use are no longer declared in.
     */
    if (!init && !cond_decl) return;
    if (lparen.isInvalid() || rparen.isInvalid()) return;
    const clang::VarDecl* var = cond_decl ? llvm::dyn_cast<clang::VarDecl>(cond_decl->getSingleDecl()) : nullptr;
    if (cond_decl && (!var || llvm::isa<clang::DecompositionDecl>(var))) return;
    auto has_call = [&](clang::Stmt* part) {
        if (auto* expr = llvm::dyn_cast_or_null<clang::Expr>(part)) return exprHasFuncCall(expr);
        if (auto* decl_stmt = llvm::dyn_cast_or_null<clang::DeclStmt>(part))
            return llvm::any_of(decl_stmt->decls(), [&](clang::Decl* decl) {
                auto* declared = llvm::dyn_cast<clang::VarDecl>(decl);
                return declared && declared->getInit() && exprHasFuncCall(declared->getInit());
            });
        return false;
    };
    if (!has_call(init) && !has_call(cond_decl) && !has_call(cond)) return;

    const clang::SourceManager& sm = m_ctx.getSourceManager();
    // the init statement with its `;`, as it is written between the parentheses
    clang::SourceLocation cond_begin = cond_decl ? cond_decl->getBeginLoc() : cond->getBeginLoc();
    std::string head = m_rewriter.getRewrittenText(clang::CharSourceRange::getCharRange(lparen.getLocWithOffset(1), cond_begin));
    std::string test;
    if (cond_decl) {
        head += m_rewriter.getRewrittenText(clang::CharSourceRange::getTokenRange(cond_decl->getSourceRange())) + ";";
        test = var->getNameAsString();
    } else {
        test = m_rewriter.getRewrittenText(clang::CharSourceRange::getTokenRange(cond->getSourceRange()));
    }
    clang::SourceLocation end = clang::Lexer::getLocForEndOfToken(stmt->getEndLoc(), 0, sm, m_ctx.getLangOpts());
    if (head.empty() || test.empty() || end.isInvalid()) return;

    m_rewriter.ReplaceText(clang::CharSourceRange::getCharRange(lparen.getLocWithOffset(1), rparen), test);
    m_rewriter.InsertTextBefore(stmt->getBeginLoc(), "{\n" + head + "\n");
    m_rewriter.InsertTextAfter(end, "\n}");
}

bool LoopToIRVisitor::VisitDoStmt(clang::DoStmt* do_stmt) {
    /**
     * `do body while (cond);`
     * is transformed into
     * do {
     *   { body }
     *   if (!(cond)) { break; }
     * } while(1);
     *
     * {if body has a continue}
     * `do body while (cond);`
     * is transformed into
     * do {
     *   { body [continue replaced by goto __loop_continue_#id] }
     *   __loop_continue_#id: { if (!(cond)) { break; } }
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

    std::string label = jumpContinuesToLabel(do_stmt->getBody());
    if (!label.empty()) cond_wrapped_in_if = label + ": {\n" + cond_wrapped_in_if + "}\n";

    m_rewriter.InsertTextBefore(do_stmt->getBody()->getSourceRange().getBegin().getLocWithOffset(1), "{\n");
    m_rewriter.InsertTextBefore(do_stmt->getBody()->getSourceRange().getEnd(), "}\n" + cond_wrapped_in_if);

    return true;
}

// continues of the loop whose body is stmt, nested loops own theirs
void LoopToIRVisitor::collectContinues(clang::Stmt* stmt, std::vector<clang::ContinueStmt*>& continues) {
    if (!stmt) return;
    if (auto* cont = llvm::dyn_cast<clang::ContinueStmt>(stmt)) {
        continues.push_back(cont);
        return;
    }
    if (llvm::isa<clang::ForStmt, clang::WhileStmt, clang::DoStmt, clang::CXXForRangeStmt>(stmt)) return;
    for (clang::Stmt* child : stmt->children())
        collectContinues(child, continues);
}

// turns the continues of the loop into gotos to a new label, empty if there are none
std::string LoopToIRVisitor::jumpContinuesToLabel(clang::Stmt* body) {
    std::vector<clang::ContinueStmt*> continues;
    collectContinues(body, continues);
    if (continues.empty()) return "";

    std::string label = "__loop_continue_" + std::to_string(m_label_index++);
    for (clang::ContinueStmt* cont : continues)
        m_rewriter.ReplaceText(cont->getSourceRange(), "goto " + label);
    return label;
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
