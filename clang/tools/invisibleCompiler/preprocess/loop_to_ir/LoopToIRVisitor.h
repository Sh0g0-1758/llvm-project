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

// --- LoopToIRVisitor --------------------------------------

// loop-to-ir: writes the condition and increment of a loop that calls something as statements
// of its body, `if (!(cond)) break;`, so that their calls can be hoisted, and moves the
// declarations of conditions out of if and switch, see docs/DESIGN.md section 5.10.
class LoopToIRVisitor : public LaterPassVisitor<LoopToIRVisitor> {
  public:
    LoopToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    // `for (; i < size(); i = next(i))` => `for (;;) { if (!(i < size())) break; ... i = next(i); }`
    bool VisitForStmt(clang::ForStmt* for_stmt);
    // `while (more())` => `while (1) { if (!(more())) break; ... }`
    bool VisitWhileStmt(clang::WhileStmt* while_stmt);
    // `do ... while (more());` => `do { ... if (!(more())) break; } while (1);`
    bool VisitDoStmt(clang::DoStmt* do_stmt);
    // `if (auto p = find())` => `{ auto p = find(); if (p) ... }`
    bool VisitIfStmt(clang::IfStmt* if_stmt);
    // `switch (int c = next())` => `{ int c = next(); switch (c) ... }`
    bool VisitSwitchStmt(clang::SwitchStmt* switch_stmt);
  private:
    // moves the init statement and condition variable of an if or switch @p stmt before it
    void moveConditionDeclsOut(clang::Stmt* stmt, clang::Stmt* init, clang::DeclStmt* cond_decl, clang::Expr* cond,
                               clang::SourceLocation lparen, clang::SourceLocation rparen);
    // the continues of the loop whose body is @p stmt, not those of nested loops
    void collectContinues(clang::Stmt* stmt, std::vector<clang::ContinueStmt*>& continues);
    // `continue;` => `goto __loop_continue_N;`, returning the label, or empty without continues
    std::string jumpContinuesToLabel(clang::Stmt* body);
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    // the N of the next `__loop_continue_N`
    size_t m_label_index = 0;
};

}  // namespace invisible
