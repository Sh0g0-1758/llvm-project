#pragma once

#include "helpers.h"
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

// ─── CommaInitVisitor ──────────────────────────────────────────────

/// Splits comma-separated variable declarations into individual
/// statements (e.g. `int a, b = 1;` → `int a;\nint b = 1;\n`).
class CommaInitVisitor : public clang::RecursiveASTVisitor<CommaInitVisitor> {
  public:
    bool TraverseDecl(clang::Decl* decl) {
        return isLeftAsWritten(decl) || clang::RecursiveASTVisitor<CommaInitVisitor>::TraverseDecl(decl);
    }
    CommaInitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);

    bool VisitDeclStmt(clang::DeclStmt* decl_stmt);
    clang::SourceLocation findInsertionLoc(clang::DeclStmt* decl_stmt);

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
};

}  // namespace invisible
