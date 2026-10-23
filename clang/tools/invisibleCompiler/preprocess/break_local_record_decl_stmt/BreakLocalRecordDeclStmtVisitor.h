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

// --- BreakLocalRecordDeclStmtVisitor --------------------------------------

// Run by decl-before-def on its AST, see docs/DESIGN.md section 6.4. In an instantiation, it
// splits a declaration of a local class together with variables into two DeclStmts, eg.
//
//     template <class T> void f() { struct P { T x; } a, b; }
//
// When printing f<int>'s body, clang's printer writes the variables but not the class's
// definition. Split, it prints `struct P { int x; };` and then `P a, b;`.
class BreakLocalRecordDeclStmtVisitor : public clang::RecursiveASTVisitor<BreakLocalRecordDeclStmtVisitor> {
  public:
    BreakLocalRecordDeclStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    // splits the DeclStmt of @p record_decl, a local class of an implicit instantiation
    bool VisitCXXRecordDecl(clang::CXXRecordDecl* record_decl);
    // the instantiations are what decl-before-def prints
    bool shouldVisitTemplateInstantiations() const;
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
};

}  // namespace invisible
