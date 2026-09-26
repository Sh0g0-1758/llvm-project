#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <string>

namespace invisible {

// ─── DisassembleCXXForRangeStmtVisitor ────────────────────

/// Writes range based for loops as for loops, with the implicit range, begin
/// and end variables declared before the loop
class DisassembleCXXForRangeStmtVisitor : public clang::RecursiveASTVisitor<DisassembleCXXForRangeStmtVisitor> {
  public:
    DisassembleCXXForRangeStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitCXXForRangeStmt(clang::CXXForRangeStmt* for_stmt);
  private:
    std::string print(clang::Stmt* stmt);
    std::string sourceText(clang::CharSourceRange range);
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    size_t m_current_index = 0;
};

}  // namespace invisible
