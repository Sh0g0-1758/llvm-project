#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <map>
#include <string>
#include <vector>

namespace invisible {

struct InlineEntry {
    std::string caller;
    std::string callee;
    size_t line;
    std::string file;
};

// ─── CommaInitVisitor ──────────────────────────────────────────────

/// Splits comma-separated variable declarations into individual
/// statements (e.g. `int a, b = 1;` → `int a;\nint b = 1;\n`).
class CommaInitVisitor : public clang::RecursiveASTVisitor<CommaInitVisitor> {
  public:
    CommaInitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);

    bool VisitDeclStmt(clang::DeclStmt* decl_stmt);
    clang::SourceLocation findInsertionLoc(clang::DeclStmt* decl_stmt);

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
};

// ─── TemplateSpecializerVisitor ──────────────────────────────────────────────

/// Specializes Templates so that transformations can be matched to actual source code
class TemplateSpecializerVisitor : public clang::RecursiveASTVisitor<TemplateSpecializerVisitor> {
  public:
    TemplateSpecializerVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitFunctionTemplateDecl(clang::FunctionTemplateDecl *FTD);
    bool VisitClassTemplateDecl(clang::ClassTemplateDecl *CTD);

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    std::string emitFunctionSpecialization(clang::FunctionDecl *Spec);
    clang::SourceLocation getInsertionPoint(clang::FunctionTemplateDecl *FTD, clang::SourceManager &SM);
    clang::SourceLocation getInsertionPoint(clang::ClassTemplateDecl *CTD, clang::SourceManager &SM);
    std::string emitClassSpecialization(clang::ClassTemplateSpecializationDecl *Spec);
};

// ─── FuncCallCleanupVisitor ────────────────────────────────────────

/// Hoists nested function calls out of argument positions into
/// temporary variables so each call is a standalone statement.
///
/// On each pass, all innermost nested calls (those whose arguments
/// contain no further nested calls) are hoisted in one traversal.
/// This avoids the O(N) re-parse overhead of hoisting one call at a
/// time.
class FuncCallCleanupVisitor : public clang::RecursiveASTVisitor<FuncCallCleanupVisitor> {
  public:
    FuncCallCleanupVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);

    bool VisitCallExpr(clang::CallExpr* call);
    bool did_cleanup();

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::SourceLocation m_last_update_loc;

    clang::SourceLocation findInsertionLoc(const clang::CallExpr* call);
    bool isTopLevelCall(const clang::CallExpr* call);
};

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

// ─── InlinePrinterHelper ───────────────────────────────────────────

/// Custom PrinterHelper that renames local variables with a prefix
/// to avoid collisions, substitutes parameter references with
/// caller arguments, and rewrites return statements as assignments.
class InlinePrinterHelper : public clang::PrinterHelper {
  public:
    InlinePrinterHelper(clang::ASTContext& ctx, std::string prefix, std::string target_var,
                        std::map<int, std::string> caller_params, clang::PrintingPolicy policy);

    bool handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) override;

  private:
    clang::ASTContext& m_ctx;
    std::string m_prefix;
    std::string m_target_var;
    std::map<int, std::string> m_caller_params;
    clang::PrintingPolicy m_policy;
    void renameVLASizeVars(clang::QualType T, clang::ASTContext &Ctx,
                    std::vector<std::pair<clang::VarDecl*, clang::IdentifierInfo*>> &renamed);
};

// ─── InlineVisitor ─────────────────────────────────────────────────

/// Inlines a specific callee into a caller at a given call site,
/// guided by an oracle file.
class InlineVisitor : public clang::RecursiveASTVisitor<InlineVisitor> {
  public:
    InlineVisitor(clang::ASTContext& ctx, const std::string& caller, const std::string& callee,
                  std::vector<std::pair<size_t, std::string>>& call_sites, size_t current_index,
                  clang::Rewriter& rewriter);

    bool VisitFunctionDecl(clang::FunctionDecl* func);
    bool VisitCallExpr(clang::CallExpr* call);
    bool VisitCXXConstructExpr(clang::CXXConstructExpr* expr);
    bool shouldVisitImplicitCode() const;

    /// Whether this visitor successfully inlined a call site.
    bool matched() const;

  private:
    clang::ASTContext& m_ctx;
    std::string m_caller;
    std::string m_callee;
    std::vector<std::pair<size_t, std::string>>& m_call_sites;
    size_t m_current_index;
    clang::Rewriter& m_rewriter;
    std::string m_current_func;
    bool m_matched = false;
};

// ─── ConstantFoldVisitor ───────────────────────────────────────────

class constantFoldVisitor : public clang::RecursiveASTVisitor<constantFoldVisitor> {
private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::SourceLocation m_last_update_loc_begin;
    clang::SourceLocation m_last_update_loc_end;

public:
    constantFoldVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    virtual bool VisitExpr(clang::Expr* expr);
    bool did_fold();
};

// ─── Free functions ────────────────────────────────────────────────

/// Split comma-separated declarations into individual statements and
/// hoist nested function calls into temporaries.  Returns the
/// transformed source.
std::string preprocess(const std::string& source, const std::string& filename, std::vector<std::string>& args);

std::string constant_fold(const std::string& source, const std::string& filename, std::vector<std::string>& args);

/// Extract all function definitions from @p source and return them
/// as a JSON string mapping function names to their printed bodies.
std::string separate_functions(const std::string& source, const std::string& filename, std::vector<std::string>& args);

struct InlineResult {
    std::string source;
    std::vector<bool> succeeded;
};

/// Inline function calls described by @p entries into their callers.
/// Returns the transformed source and a per-entry success vector.
InlineResult inline_functions(const std::string& source, const std::string& filename,
                              const std::vector<InlineEntry>& entries, std::vector<std::string>& args);

}  // namespace invisible
