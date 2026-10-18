#pragma once

#include "helpers.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <string>

namespace invisible {

// ─── DisassembleCXXForRangeStmtVisitor ────────────────────

/// disassemble-cxx-for-range-stmt: writes a range-based for as the iterator loop it stands for,
/// whose begin, end, comparison, increment and dereference calls can then be hoisted, see
/// docs/DESIGN.md section 5.9.
class DisassembleCXXForRangeStmtVisitor : public LaterPassVisitor<DisassembleCXXForRangeStmtVisitor> {
  public:
    DisassembleCXXForRangeStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    /// `for (auto& x : v) f(x);` => the iterator loop it stands for, see the definition
    bool VisitCXXForRangeStmt(clang::CXXForRangeStmt* for_stmt);
  private:
    /// @p stmt printed with its names qualified, eg. the begin expression `::std::begin(v)`
    std::string print(clang::Stmt* stmt);
    /// the text of @p range as written
    std::string sourceText(clang::CharSourceRange range);
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    /// the N of the variables `__for_begin_N`, `__for_end_N` and `__for_range_N`
    size_t m_current_index = 0;
};

}  // namespace invisible
