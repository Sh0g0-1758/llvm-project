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

/// Canonicalize all local / global variables defined in a translation unit. 
/// The trick here is to make the changes within the AST so that the changes 
/// propagate to the DeclRefExpr. 
class CanonicalizeVarDeclVisitor : public clang::RecursiveASTVisitor<CanonicalizeVarDeclVisitor> {
  public:
    CanonicalizeVarDeclVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitVarDecl(clang::VarDecl* decl);
    bool VisitParmVarDecl(clang::ParmVarDecl* decl);
    bool shouldVisitTemplateInstantiations() const;
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    size_t uniqueIndx;
    std::unordered_map<clang::VarDecl*, std::string> m_var_renames;
};

}  // namespace invisible
