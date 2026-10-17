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

/// name-template-type-parm: names the unnamed template parameters, eg. `template <class = void>`
/// becomes `template <class __type_param__0 = void>`, which the declarations decl-before-def prints
/// for the template's members can then refer to, see docs/DESIGN.md section 5.3.
class NameTemplateTypeParmVisitor : public clang::RecursiveASTVisitor<NameTemplateTypeParmVisitor> {
  public:
    NameTemplateTypeParmVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    /// `template <class = void>` → `template <class __type_param__0 = void>`
    bool VisitTemplateTypeParmDecl(clang::TemplateTypeParmDecl* decl);
    /// `template <int>` → `template <int __non_type_param__0 >`
    bool VisitNonTypeTemplateParmDecl(clang::NonTypeTemplateParmDecl* decl);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    /// numbers the names given, across the TU
    size_t m_index;
};

}  // namespace invisible
