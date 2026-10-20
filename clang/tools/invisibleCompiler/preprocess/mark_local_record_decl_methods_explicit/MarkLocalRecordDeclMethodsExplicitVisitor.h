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

// --- MarkLocalRecordDeclMethodsExplicitVisitor --------------------------------------

// Run by decl-before-def on its AST, see docs/DESIGN.md section 6.4: marks the members of a local
// class of an instantiation as explicit specializations, eg. `get` in
//
//     template <class T> auto f() { struct Box { T v; T get() { return v; } }; return Box{}; }
//
// The printer leaves an implicit instantiation out of the class it prints, so f<int>'s copy or
// specialization would print `struct Box { int v; };` without `get`. Marked explicit, the member
// is printed in its class.
class MarkLocalRecordDeclMethodsExplicitVisitor : public clang::RecursiveASTVisitor<MarkLocalRecordDeclMethodsExplicitVisitor> {
  public:
    MarkLocalRecordDeclMethodsExplicitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    // marks @p func explicit when it is an implicitly instantiated member of a local class
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    // the members of local classes of instantiations are only seen in the instantiations
    bool shouldVisitTemplateInstantiations() const;
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
};

}  // namespace invisible
