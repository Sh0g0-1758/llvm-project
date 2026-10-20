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

// --- FilterVisitor -------------------------------------------------

// Collects every function definition of a TU, printed, by its unqualified name, eg. for
//
//     int square(int x) { return x * x; }
//
// the entry `"square": "int square(int x) {\n  return x * x;\n}\n"`. It feeds tooling that
// wants each function on its own. Functions of the same name, eg. overloads or members of
// different classes, overwrite each other: the last one visited is kept.
class FilterVisitor : public clang::RecursiveASTVisitor<FilterVisitor> {
  public:
    FilterVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);

    bool VisitFunctionDecl(clang::FunctionDecl* func);

    // The collected functions as a JSON object of name to printed definition, indented by 2.
    std::string toJSON();

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    std::map<std::string, std::string> m_functions;
};

// The `separate` mode: parses @p source and returns its function definitions as JSON, see FilterVisitor.
std::string separate_functions(const std::string& source, const std::string& filename, std::vector<std::string>& args);


}  // namespace invisible
