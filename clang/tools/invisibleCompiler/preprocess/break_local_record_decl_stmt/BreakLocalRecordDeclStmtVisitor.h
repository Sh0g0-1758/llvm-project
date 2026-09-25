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

// ─── BreakLocalRecordDeclStmtVisitor ──────────────────────────────────────

/// Breaks a local record decl's DeclStmt into two parts. One with the recordDecl
/// and the other with varDecls, if the DeclStmt has multiple decls with it. 
class BreakLocalRecordDeclStmtVisitor : public clang::RecursiveASTVisitor<BreakLocalRecordDeclStmtVisitor> {
  public:
    BreakLocalRecordDeclStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitCXXRecordDecl(clang::CXXRecordDecl* record_decl);
    bool shouldVisitTemplateInstantiations() const;
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
};

}  // namespace invisible
