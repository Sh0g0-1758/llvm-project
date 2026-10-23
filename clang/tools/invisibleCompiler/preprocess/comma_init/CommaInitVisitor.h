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

// --- CommaInitVisitor ----------------------------------------------

// comma-init: splits a declaration of several variables into one declaration each, eg.
// `int a, b = f();` into `int a;` and `int b = f();`, see docs/DESIGN.md section 5.11.
class CommaInitVisitor : public LaterPassVisitor<CommaInitVisitor> {
  public:
    CommaInitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);

    // `int a, b = f();` => `int a;` and `int b = f();`
    bool VisitDeclStmt(clang::DeclStmt* decl_stmt);
    // the start of the statement that holds @p decl_stmt and is directly in a block
    clang::SourceLocation findInsertionLoc(clang::DeclStmt* decl_stmt);

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    // set by findInsertionLoc when the declaration is in a body without braces, see isUnbracedBody
    const clang::Stmt* m_unbraced = nullptr;
};

}  // namespace invisible
