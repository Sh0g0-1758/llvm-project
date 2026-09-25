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

// ─── MarkLocalRecordDeclMethodsExplicitVisitor ──────────────────────────────────────

/// Marks an implicit function instantiation in a local record declaration 
/// explicit so that it shows up when function specializations are printed. 
class MarkLocalRecordDeclMethodsExplicitVisitor : public clang::RecursiveASTVisitor<MarkLocalRecordDeclMethodsExplicitVisitor> {
  public:
    MarkLocalRecordDeclMethodsExplicitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    bool shouldVisitTemplateInstantiations() const;
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
};

}  // namespace invisible
