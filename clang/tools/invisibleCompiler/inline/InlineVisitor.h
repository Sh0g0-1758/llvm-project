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

struct InlineEntry {
    std::string caller;
    std::string callee;
    size_t line;
    std::string file;
};

// ─── FunctionParamRenamer ───────────────────────────────────────────

/// incorporates function param changes within the AST itself. This is because
/// llvm's printerHelper is structured in such a way that does not allow the 
/// declStmt varDecl's init expression to be hijacked by a custom printer. 
struct FunctionParamRenamer : clang::RecursiveASTVisitor<FunctionParamRenamer> {
public:
    FunctionParamRenamer(clang::ASTContext &C, std::map<int, std::string> &PR);
    bool VisitDeclRefExpr(clang::DeclRefExpr *DRE);
private:
    clang::ASTContext &Ctx;
    std::map<int, std::string> &callerParams;
};

// ─── InlinePrinterHelper ───────────────────────────────────────────

/// Custom PrinterHelper that renames local variables with a prefix
/// to avoid collisions, substitutes parameter references with
/// caller arguments, and rewrites return statements as assignments.
class InlinePrinterHelper : public clang::PrinterHelper {
  public:
    InlinePrinterHelper(clang::ASTContext& ctx, std::string target_var,
                        std::map<int, std::string> caller_params, clang::PrintingPolicy policy,
                        size_t current_index);

    bool handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) override;

  private:
    clang::ASTContext& m_ctx;
    std::string m_target_var;
    std::map<int, std::string> m_caller_params;
    clang::PrintingPolicy m_policy;
    size_t m_current_index;
    std::map<std::string, std::string> m_goto_labels;
    void renameFuncParamInType(clang::QualType T);
    FunctionParamRenamer m_func_param_renamer;
    size_t m_goto_label_index;
};

// ─── InlineVisitor ─────────────────────────────────────────────────

/// Inlines a specific callee into a caller at a given call site,
/// guided by an oracle file.
class InlineVisitor : public clang::RecursiveASTVisitor<InlineVisitor> {
  public:
    InlineVisitor(clang::ASTContext& ctx, const std::string& caller, const std::string& callee,
                  std::vector<size_t>& call_sites, size_t& current_index,
                  clang::Rewriter& rewriter, std::map<std::string, clang::FunctionDecl*>& decl_map);
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    bool VisitCallExpr(clang::CallExpr* call);
    bool isTopLevelCall(const clang::CallExpr* call);
    bool VisitCXXConstructExpr(clang::CXXConstructExpr* expr);
    bool shouldVisitImplicitCode() const;
    bool isTransparentParent(const clang::Stmt* s, bool isExplicitCoversionOperator);
    clang::DynTypedNode getSemanticParent(const clang::Stmt* node, bool isExplicitCoversionOperator);

    /// Whether this visitor successfully inlined a call site.
    bool matched() const;
    bool callInLoop(const clang::CallExpr* call);
    size_t get_net_change();

  private:
    clang::ASTContext& m_ctx;
    std::string m_caller;
    std::string m_callee;
    std::vector<size_t>& m_call_sites;
    size_t& m_current_index;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    std::string m_current_func;
    bool m_matched = false;
    size_t m_net_lines = 0;
    std::map<std::string, clang::FunctionDecl*>& m_function_decl_map;
};

// ─── FunctionDeclMapVisitor ─────────────────────────────────────────────────

/// Inlines a specific callee into a caller at a given call site,
/// guided by an oracle file.
class FunctionDeclMapVisitor : public clang::RecursiveASTVisitor<FunctionDeclMapVisitor> {
  public:
    FunctionDeclMapVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    std::map<std::string, clang::FunctionDecl*>& get_function_decl_map();

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    std::map<std::string, clang::FunctionDecl*> m_function_decl_map;
};

struct InlineResult {
    std::string source;
    std::vector<bool> succeeded;
};

/// Inline function calls described by @p entries into their callers.
/// Returns the transformed source and a per-entry success vector.
InlineResult inline_functions(const std::string& source, const std::string& filename,
                              const std::vector<InlineEntry>& entries, std::vector<std::string>& args);



}  // namespace invisible
