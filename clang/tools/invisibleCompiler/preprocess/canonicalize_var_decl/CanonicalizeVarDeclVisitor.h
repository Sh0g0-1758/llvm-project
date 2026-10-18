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

// ─── CanonicalizeVarDeclVisitor ──────────────────────────────────────

/// Run by decl-before-def on its AST, see docs/DESIGN.md section 6.4: renames every local variable
/// and parameter of the TU to a name unique in it, `___var__N`, eg.
///
///     int sq(int x) { int y = x * x; return y; }   =>   int sq(int ___var__0) { int ___var__1 = ___var__0 * ___var__0; return ___var__1; }
///
/// The inliner pastes a callee's body into its caller, where a local of the callee could clash
/// with or shadow one of the caller. The names are changed in the AST, so that every reference,
/// a DeclRefExpr pointing at the declaration, prints the new name too. Globals keep their names.
class CanonicalizeVarDeclVisitor : public clang::RecursiveASTVisitor<CanonicalizeVarDeclVisitor> {
  public:
    CanonicalizeVarDeclVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    /// renames a local variable or a parameter, not a block scope `extern`, a structured binding or a requires parameter
    bool VisitVarDecl(clang::VarDecl* decl);
    /// the locals of instantiations are printed by decl-before-def too
    bool shouldVisitTemplateInstantiations() const;
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    /// the N of the next `___var__N`
    size_t m_next_index;
};

}  // namespace invisible
