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

// --- ShortCircuitOperatorToIRVisitor --------------------------------------

// short-circuit-to-ir: spells out `&&`, `||` and `?:` whose operands call something as ifs, so
// that hoisting a call keeps it conditional, see docs/DESIGN.md section 5.12.
class ShortCircuitOperatorToIRVisitor : public LaterPassVisitor<ShortCircuitOperatorToIRVisitor> {
  public:
    ShortCircuitOperatorToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, size_t& current_index);
    // `a() && b()` => an if setting `__short_circuit_result_N`, see the definition
    bool VisitBinaryOperator(clang::BinaryOperator* binary_op);
    // `c ? f() : g()` => an if/else setting `__short_circuit_result_N`, see the definition
    bool VisitConditionalOperator(clang::ConditionalOperator* conditional_op);
    // the start of the statement of the nearest block that holds the operator, where the if goes
    clang::SourceLocation findInsertionLoc(const clang::Expr* expr);
    // whether this iteration rewrote anything, so that the pass runs again
    bool didConvertToIR();
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    // the N of the next `__short_circuit_result_N`, shared with hoist-calls
    size_t& m_current_index;
    // the raw location of the statement last rewritten. One operator per statement and iteration:
    // once an operator is rewritten, the text of an operator inside it is gone.
    size_t m_last_update_loc;
};

}  // namespace invisible
