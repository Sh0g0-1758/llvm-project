#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Sema/Sema.h"

namespace invisible {

// ─── FunctionalCastToCStyleCastVisitor ──────────────────────────────────────

/// A function style cast only accepts a simple type name, but once specialized
/// the substituted type may not be one, eg. `T(x)` with `T = unsigned long` is
/// printed as `unsigned long(x)` which does not parse. Such casts are rewritten
/// to the equivalent C style cast in the AST, so that every printer (including
/// the one for local classes, which ignores the printer helpers) prints them.
class FunctionalCastToCStyleCastVisitor : public clang::RecursiveASTVisitor<FunctionalCastToCStyleCastVisitor> {
  public:
    FunctionalCastToCStyleCastVisitor(clang::ASTContext& ctx, clang::Sema& sema);
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool TraverseStmt(clang::Stmt* stmt);

  private:
    void replaceInChildren(clang::Stmt* stmt);
    bool isSimpleTypeName(clang::QualType type);

    clang::ASTContext& m_ctx;
    clang::Sema& m_sema;
    clang::PrintingPolicy m_policy;
};

}  // namespace invisible
