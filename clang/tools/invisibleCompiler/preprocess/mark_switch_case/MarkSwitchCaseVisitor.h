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

// --- MarkSwitchCaseVisitor --------------------------------------

// mark-switch-case: puts an empty statement after every case label, so that hoist-calls puts a
// call's temporary under the label rather than above it, see docs/DESIGN.md section 5.7.
class MarkSwitchCaseVisitor : public LaterPassVisitor<MarkSwitchCaseVisitor> {
  public:
    MarkSwitchCaseVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    // `case 1: g(f());` => `case 1: ;` and `g(f());` on the next line
    bool VisitSwitchCase(clang::SwitchCase* label);
  private:
    clang::Rewriter& m_rewriter;
};

}  // namespace invisible
