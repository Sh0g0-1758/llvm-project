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

// ─── CompoundStmtWrapperVisitor ──────────────────────────────────────

/// compound-stmt-wrapper: wraps every for, if and switch statement in a block, so that what the
/// later passes put before it, eg. its declarations or hoisted calls, stays in its scope, and
/// the body of a switch in one, see docs/DESIGN.md section 5.6.
class CompoundStmtWrapperVisitor : public LaterPassVisitor<CompoundStmtWrapperVisitor> {
  public:
    CompoundStmtWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    /// `for (...) f();` => `{ for (...) f(); }`, and an empty body `;` => `{}`
    bool VisitForStmt(clang::ForStmt* for_stmt);
    /// `if (auto p = find()) use(p);` => `{ if (...) use(p); }`
    bool VisitIfStmt(clang::IfStmt* if_stmt);
    /// `switch (int c = next()) {...}` => `{ switch (...) {...} }`, and a body that isn't a block => `{...}`
    bool VisitSwitchStmt(clang::SwitchStmt* switch_stmt);
    /// an empty body, `while (poll());` => `while (poll()){}`
    bool VisitWhileStmt(clang::WhileStmt* while_stmt);
    /// an empty body, `do ; while (poll());` => `do {} while (poll());`
    bool VisitDoStmt(clang::DoStmt* do_stmt);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
};

}  // namespace invisible
