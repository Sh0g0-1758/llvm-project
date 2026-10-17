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

/// access-specifier: makes every member and base of a class public, see docs/DESIGN.md section
/// 5.2. The code DeclBeforeDef moves out of a class, a copy of a member and an inlined body use
/// them from outside it. With @p keep_observable_access, the bases and the constructors,
/// destructors and assignment operators keep the access they are written with, which type traits
/// can tell, eg. `is_destructible` and `is_convertible<D*, B*>`.
class AccessSpecifierVisitor : public clang::RecursiveASTVisitor<AccessSpecifierVisitor> {
  public:
    AccessSpecifierVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, bool keep_observable_access = false);
    /// `private:` / `protected:` → `public:`
    bool VisitAccessSpecDecl(clang::AccessSpecDecl* access_decl);
    /// `public:` after the opening brace, and the bases public
    bool VisitCXXRecordDecl(clang::CXXRecordDecl* record_decl);
    /// with keep_observable_access, a special member keeps its access
    bool VisitCXXMethodDecl(clang::CXXMethodDecl* method);
  private:
    /// `class D : B` → `class D : public B`, `: private B` → `: public B`
    void makePublic(const clang::CXXBaseSpecifier& base, bool in_class);
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    bool m_keep_observable_access;
};

}  // namespace invisible
