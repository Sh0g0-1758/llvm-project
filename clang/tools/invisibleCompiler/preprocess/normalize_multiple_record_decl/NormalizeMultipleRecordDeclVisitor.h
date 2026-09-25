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
#include "common/DeclBeforeDefPrinterHelper.h"

namespace invisible {

// ─── NormalizeMultipleRecordDeclVisitor ──────────────────────────────────────────────

/// Puts all function declarations before all function definitions 
class NormalizeMultipleRecordDeclVisitor : public clang::RecursiveASTVisitor<NormalizeMultipleRecordDeclVisitor> {
  public:
    NormalizeMultipleRecordDeclVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    bool VisitVarDecl(clang::VarDecl* decl);
    bool VisitFieldDecl(clang::FieldDecl* field_decl);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    clang::PrintingPolicy m_deferred_policy_def;
    DeclBeforeDefPrinterHelper m_custom_printer;
};

}  // namespace invisible
