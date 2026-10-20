#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Sema/Sema.h"

namespace invisible {

// --- FunctionalCastToCStyleCastVisitor --------------------------------------

// Run by decl-before-def on its AST, see docs/DESIGN.md section 6.4. A function style cast only
// accepts a simple type name, but once specialized the substituted type may not be one, eg.
// `T(x)` with `T = unsigned long` is printed as `unsigned long(x)`, which doesn't parse. Such
// casts are rewritten to the equivalent C style cast in the AST, `(unsigned long)(x)`, so that
// every printer (including the one for local classes, which ignores the printer helpers)
// prints them. So are `T{x}` => `(T)(x)`, `T{}` => `(T)0`, and the value initialization of a
// scalar, `T()` => `((T)0)`.
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
