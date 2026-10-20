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

// --- FuncCallCleanupVisitor ----------------------------------------

// hoist-calls: moves every call nested in another expression into a temporary of its own, so
// that each call is a statement of its own, see docs/DESIGN.md section 5.14. An iteration hoists
// at most one call out of each statement, and the pass repeats until no call is left nested.
class FuncCallCleanupVisitor : public LaterPassVisitor<FuncCallCleanupVisitor> {
  public:
    FuncCallCleanupVisitor(clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter, size_t& current_index);

    // a call in an unevaluated operand is never made, it only names a type or a
    // property of one: hoisting it would make it, and eg. turn `decltype(f())` of a
    // prvalue into a reference type. These operands are left as written.
    bool TraverseDecltypeTypeLoc(clang::DecltypeTypeLoc, bool) { return true; }
    bool TraverseTypeOfExprTypeLoc(clang::TypeOfExprTypeLoc, bool) { return true; }
    bool TraverseCXXNoexceptExpr(clang::CXXNoexceptExpr*, DataRecursionQueue* = nullptr) { return true; }
    bool TraverseRequiresExpr(clang::RequiresExpr*, DataRecursionQueue* = nullptr) { return true; }
    bool TraverseUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr* expr, DataRecursionQueue* queue = nullptr) {
        // the size of a variable length array is worked out at run time
        return !expr->getTypeOfArgument()->isVariablyModifiedType() ||
               clang::RecursiveASTVisitor<FuncCallCleanupVisitor>::TraverseUnaryExprOrTypeTraitExpr(expr, queue);
    }
    // a constant expression, eg. a template argument, a case label or the condition of `if
    // constexpr`, is evaluated by the compiler, and its calls stay in it
    bool TraverseConstantExpr(clang::ConstantExpr*, DataRecursionQueue* = nullptr) { return true; }
    bool TraverseTemplateArgumentLoc(const clang::TemplateArgumentLoc& arg) {
        return arg.getArgument().getKind() == clang::TemplateArgument::Expression ||
               LaterPassVisitor<FuncCallCleanupVisitor>::TraverseTemplateArgumentLoc(arg);
    }
    bool TraverseCXXTypeidExpr(clang::CXXTypeidExpr* expr, DataRecursionQueue* queue = nullptr) {
        // the operand of typeid is evaluated only for a glvalue of a polymorphic class
        return !expr->isPotentiallyEvaluated() ||
               clang::RecursiveASTVisitor<FuncCallCleanupVisitor>::TraverseCXXTypeidExpr(expr, queue);
    }

    // hoists @p call unless it is a statement of its own already, see topLevelStatement, or may not be
    // evaluated, eg. a branch of `?:` left as written
    bool VisitCallExpr(clang::CallExpr* call);
    // whether this iteration hoisted anything, so that the pass runs again
    bool didHoist();
    // notes whether the calls that follow are in a template, which is left as written
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    // `g(f(x));` => `T __func_hoist_ret__N = f(x);` before the statement and `g(__func_hoist_ret__N);`
    void hoistCall(clang::CallExpr* call);
  private:
    clang::ASTContext& m_ctx;
    clang::Sema& m_sema;
    clang::Rewriter& m_rewriter;
    // the statement last hoisted before, and the call: one call per statement and iteration, as
    // the rewriter can't edit text it already replaced
    clang::SourceLocation m_last_update_loc;
    clang::SourceRange m_last_call_loc;

    // the start of the statement of the nearest block holding @p call, set in @p statement, after
    // its labels; invalid where a temporary can't go, eg. a member initializer
    clang::SourceLocation findInsertionLoc(const clang::CallExpr* call, const clang::Stmt** statement);
    // whether @p statement is directly in the block of a switch, `switch (c) { case 1: f(g()); }`
    bool isDirectlyInSwitch(const clang::Stmt* statement);
    // whether @p statement is the last of a statement expression, `({ ...; f(g()); })`, whose value it is
    bool isValueOfStatementExpr(const clang::Stmt* statement);
    // whether the function or lambda @p statement is in has a label, which a goto may jump to
    bool isInFunctionWithLabels(const clang::Stmt* statement);
    // whether @p stmt is or has a label, not counting those of lambdas in it
    bool containsLabel(const clang::Stmt* stmt);
    // whether the body of a function or lambda has a label
    std::map<const clang::Stmt*, bool> m_has_labels;
    // whether the function being traversed is a template, whose calls aren't resolved
    bool m_is_uninstantiated_template;
    // the N of the next `__func_hoist_ret__N`, shared with short-circuit-to-ir
    size_t& m_current_index;
    clang::PrintingPolicy m_policy;
};

}  // namespace invisible
