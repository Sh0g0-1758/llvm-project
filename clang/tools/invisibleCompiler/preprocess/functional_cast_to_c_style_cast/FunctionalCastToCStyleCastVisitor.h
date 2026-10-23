#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Sema/Sema.h"

namespace invisible {

// --- FunctionalCastToCStyleCastVisitor --------------------------------------

// Run by decl-before-def on its AST, see docs/DESIGN.md section 6.4. A function style cast only
// takes a simple type name. In an instantiation the type may not be one: `T(x)` with
// `T = unsigned long` prints as `unsigned long(x)`, which doesn't parse. So such casts become C
// style casts in the AST, `(unsigned long)(x)`. Fixing the AST, not the printed text, also covers
// the bodies of local classes, where clang's printer doesn't call DeclBeforeDefPrinterHelper.
// Likewise `T{x}` => `(T)(x)`, `T{}` => `(T)0` and a scalar's `T()` => `((T)0)`.
class FunctionalCastToCStyleCastVisitor : public clang::RecursiveASTVisitor<FunctionalCastToCStyleCastVisitor> {
  public:
    FunctionalCastToCStyleCastVisitor(clang::ASTContext& ctx, clang::Sema& sema);
    bool shouldVisitTemplateInstantiations() const { return true; }
    // replaces the casts among the children of @p stmt, after traversing them
    bool TraverseStmt(clang::Stmt* stmt);

  private:
    // @p operand, parenthesized when a cast in front of it would bind tighter, eg. `a - b`
    clang::Expr* parenthesized(clang::Expr* operand);
    // rewrites the casts that are children of @p stmt in their slots
    void replaceInChildren(clang::Stmt* stmt);
    // whether @p type is printed as a name a function style cast accepts, eg. `size_t` but not `unsigned long`
    bool isSimpleTypeName(clang::QualType type);

    clang::ASTContext& m_ctx;
    clang::Sema& m_sema;
    clang::PrintingPolicy m_policy;
};

}  // namespace invisible
