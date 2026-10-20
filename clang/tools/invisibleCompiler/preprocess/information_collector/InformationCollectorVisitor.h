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

// --- InformationCollectorVisitor --------------------------------------

// The `info` mode, a debugging aid: prints what clang knows of every function of the TU, its
// instantiations included, eg. whether it has a body, is a template instantiation or a friend,
// to stdout. It rewrites nothing.
class InformationCollectorVisitor : public clang::RecursiveASTVisitor<InformationCollectorVisitor> {
  public:
    InformationCollectorVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    // prints the facts of @p func
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    // the instantiations are printed too
    bool shouldVisitTemplateInstantiations() const;

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
};

}  // namespace invisible
