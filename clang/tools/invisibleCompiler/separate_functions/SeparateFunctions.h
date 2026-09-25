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

// ─── FilterVisitor ─────────────────────────────────────────────────

/// Extracts all function definitions and returns them as a JSON
/// string mapping function names to their printed bodies.
class FilterVisitor : public clang::RecursiveASTVisitor<FilterVisitor> {
  public:
    FilterVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);

    bool VisitFunctionDecl(clang::FunctionDecl* func);

    /// Return the collected functions as a JSON string.
    std::string toJSON();

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    std::map<std::string, std::string> m_functions;
};

/// Extract all function definitions from @p source and return them
/// as a JSON string mapping function names to their printed bodies.
std::string separate_functions(const std::string& source, const std::string& filename, std::vector<std::string>& args);


}  // namespace invisible
