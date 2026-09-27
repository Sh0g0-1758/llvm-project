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

// ─── DeclBeforeDefPrinterHelper ───────────────────────────────────────────

/// Custom PrinterHelper to print function definitions around the printer's
/// shortcomings, eg. keeping inline assembly, `if constexpr` and structured
/// bindings intact
class DeclBeforeDefPrinterHelper : public clang::PrinterHelper {
  public:
    DeclBeforeDefPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy);

    bool handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) override;

  private:
    clang::ASTContext& m_ctx;
    clang::PrintingPolicy m_policy;
};

}  // namespace invisible
