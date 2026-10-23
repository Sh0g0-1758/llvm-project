#pragma once

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

// --- MaterializeLambdaVisitor --------------------------------------

// materialize-lambda: turns a lambda into a named struct, its captures into members and its body
// into the call operator, so that its type can be named, eg. by the specialization of a
// template it is passed to, see docs/DESIGN.md section 5.4. A captured `this` becomes the member
// `__parent_this__`. Inner lambdas are turned first, and the pass runs again for the outer ones.
class MaterializeLambdaVisitor : public clang::RecursiveASTVisitor<MaterializeLambdaVisitor> {
  public:
    MaterializeLambdaVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    // `[&n](int x) { return x + n; }` => `__invisible_lambda_1a2b3c4d5e_0{n}`, the struct put before the function
    bool VisitLambdaExpr(clang::LambdaExpr* lambda);
    // whether a lambda became a struct, so that the pass runs again for the lambdas around it
    bool changed() const { return m_changed; }
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    // prints the members' types canonically, as the struct is outside the function their aliases may be in
    clang::PrintingPolicy m_field_policy;
    bool m_changed = false;
    // the index of the next struct of each function, keyed by its struct name prefix, see VisitLambdaExpr
    std::map<std::string, unsigned> m_next_index;
    // whether @p node has a lambda in it, which must become a struct first
    bool containsNestedLambda(clang::Stmt* node);
    // the function the struct is put before: the nearest one that is neither a lambda nor in a function
    clang::FunctionDecl* enclosingFunction(const clang::LambdaExpr* lambda);
    // whether the lambda is used as a function pointer, which the struct can't be
    bool convertsToFunctionPointer(const clang::LambdaExpr* lambda);
    // where the struct is inserted: before the function, its template heads and attributes
    clang::SourceLocation structLoc(const clang::FunctionDecl* enclosing_func);
};

}  // namespace invisible
