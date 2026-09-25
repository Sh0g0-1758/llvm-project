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

// ─── MaterializeLambdaVisitor ──────────────────────────────────────

/// Materialize Lamda Expressions as individual RecordDecls. Captured variables
/// are represented as field members and captured `this` keyword is renamed to a 
/// custom field member. The function represented by the lamda is emitted as an
/// operator() call and the lamdaExpression itself is replaced by an object of 
/// the corresponding recordDecl. 
class MaterializeLambdaVisitor : public clang::RecursiveASTVisitor<MaterializeLambdaVisitor> {
  public:
    MaterializeLambdaVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitLambdaExpr(clang::LambdaExpr* lambda);
    bool changed() const { return m_changed; }
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    clang::PrintingPolicy m_field_policy;
    bool m_changed = false;
    static size_t s_counter;
    bool containsNestedLambda(clang::Stmt* node);
    void rewriteCapturedThis(clang::Stmt* node, clang::Expr* replacement);
};

}  // namespace invisible
