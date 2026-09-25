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

// ─── NameTemplateTypeParmVisitor ──────────────────────────────────────

/// materializes a name for unnamed template types
/// eg. converts template<typename = void> to template<typename `id` = void>
class NameTemplateTypeParmVisitor : public clang::RecursiveASTVisitor<NameTemplateTypeParmVisitor> {
  public:
    NameTemplateTypeParmVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitTemplateTypeParmDecl(clang::TemplateTypeParmDecl* decl);
    bool VisitNonTypeTemplateParmDecl(clang::NonTypeTemplateParmDecl* decl);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    size_t m_index;
};

}  // namespace invisible
