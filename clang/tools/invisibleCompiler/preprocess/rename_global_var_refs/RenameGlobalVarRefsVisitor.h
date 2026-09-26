#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"

namespace invisible {

// ─── RenameGlobalVarRefsVisitor ──────────────────────────────────────

/// CanonicalizeVarDeclVisitor renames variables in the AST only, so the source
/// text still spells their old names. Declarations other than functions are kept
/// as written, hence every renamed global variable and every reference to it is
/// respelled in place with its canonicalized name. Function declarations are
/// skipped as DeclBeforeDefVisitor re-prints them from the AST. Template
/// instantiations are not traversed as they share the pattern's text.
class RenameGlobalVarRefsVisitor : public clang::RecursiveASTVisitor<RenameGlobalVarRefsVisitor> {
  public:
    RenameGlobalVarRefsVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool TraverseDecl(clang::Decl* decl);
    bool VisitVarDecl(clang::VarDecl* decl);
    bool VisitDeclRefExpr(clang::DeclRefExpr* ref);

  private:
    void rename(clang::SourceLocation loc, clang::VarDecl* var);

    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
};

}  // namespace invisible
