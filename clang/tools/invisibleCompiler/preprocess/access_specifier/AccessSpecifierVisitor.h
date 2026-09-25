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

// ─── AccessSpecifierVisitor ──────────────────────────────────────

/// marks every access specifier as public and unconditionally
/// adds a public access specifier at the start of every record
class AccessSpecifierVisitor : public clang::RecursiveASTVisitor<AccessSpecifierVisitor> {
  public:
    AccessSpecifierVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitAccessSpecDecl(clang::AccessSpecDecl* access_decl);
    bool VisitCXXRecordDecl(clang::CXXRecordDecl* record_decl);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
};

}  // namespace invisible
