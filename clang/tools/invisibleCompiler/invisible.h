#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Sema/Sema.h"

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

// ─── DeclBeforeDefVisitor ──────────────────────────────────────────────

/// Puts all function declarations before all function definitions 
class DeclBeforeDefVisitor : public clang::RecursiveASTVisitor<DeclBeforeDefVisitor> {
  public:
    DeclBeforeDefVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    std::string getDeferredDefinitions();
  private:
    std::vector<std::string> m_deferred_definitions;
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
};

// ─── ShortCircuitOperatorToIRVisitor ──────────────────────────────────────

/// Writes short-circuit operations in their IR form at the source code level
class ShortCircuitOperatorToIRVisitor : public clang::RecursiveASTVisitor<ShortCircuitOperatorToIRVisitor> {
  public:
    ShortCircuitOperatorToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, size_t& current_index);
    bool VisitBinaryOperator(clang::BinaryOperator* binary_op);
    bool VisitConditionalOperator(clang::ConditionalOperator* conditional_op);
    clang::SourceLocation findInsertionLoc(const clang::BinaryOperator* binary_op);
    clang::SourceLocation findInsertionLoc(const clang::ConditionalOperator* conditional_op);
    bool exprHasFuncCall(clang::Expr* expr);
    bool didConvertToIR();
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    size_t& m_current_index;
    size_t m_last_update_loc;
};

// ─── BreakElseIfVisitor ──────────────────────────────────────

/// Breaks `else if` statements into `else { if  }` statements if 
/// the expr in `if(expr)` contains a function-call. 
class BreakElseIfVisitor : public clang::RecursiveASTVisitor<BreakElseIfVisitor> {
  public:
    BreakElseIfVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitIfStmt(clang::IfStmt* if_stmt);
    bool exprHasFuncCall(clang::Expr* expr);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
};

// ─── MarkSwitchCaseVisitor ──────────────────────────────────────

/// Marks the beginning of every case statement with a semi-colon so
/// that any function call present at the start of the set of statements
/// for case is hoisted as part of the case and not above it. 
class MarkSwitchCaseVisitor : public clang::RecursiveASTVisitor<MarkSwitchCaseVisitor> {
  public:
    MarkSwitchCaseVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitCaseStmt(clang::CaseStmt* case_stmt);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
};

// ─── LoopToIRVisitor ──────────────────────────────────────

/// Writes Loops in their IR form at the source code level
class LoopToIRVisitor : public clang::RecursiveASTVisitor<LoopToIRVisitor> {
  public:
    LoopToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitForStmt(clang::ForStmt* for_stmt);
    bool VisitWhileStmt(clang::WhileStmt* while_stmt);
    bool VisitDoStmt(clang::DoStmt* do_stmt);
    bool exprHasFuncCall(clang::Expr* expr);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
};

// ─── TemplateSpecializerVisitor ──────────────────────────────────────────────

/// Specializes Templates so that transformations can be matched to actual source code
class TemplateSpecializerVisitor : public clang::RecursiveASTVisitor<TemplateSpecializerVisitor> {
  public:
    TemplateSpecializerVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, clang::Sema&);
    bool VisitFunctionTemplateDecl(clang::FunctionTemplateDecl *FTD);
    bool VisitClassTemplateDecl(clang::ClassTemplateDecl *CTD);

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::Sema& m_sema;
    std::string emitFunctionSpecialization(clang::FunctionDecl *Spec, bool);
    clang::SourceLocation getInsertionPoint(clang::FunctionTemplateDecl *FTD, clang::SourceManager &SM);
    clang::SourceLocation getInsertionPoint(clang::ClassTemplateDecl *CTD, clang::SourceManager &SM);
    std::string emitClassSpecialization(clang::ClassTemplateSpecializationDecl *Spec, bool);
    void fixDoubleAngleBrackets(std::string&);
    void instantiate_class_vars(clang::ClassTemplateSpecializationDecl *Spec);
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
    FuncCallCleanupVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, size_t& current_index);

    bool VisitCallExpr(clang::CallExpr* call);
    bool did_cleanup();
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    bool VisitDeclStmt(clang::DeclStmt* decl_stmt);
    void hoist_call_at_nearest_compound_statement(clang::CallExpr* call);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::SourceLocation m_last_update_loc;

    clang::SourceLocation findInsertionLoc(const clang::CallExpr* call);
    bool isTopLevelCall(const clang::CallExpr* call);
    bool m_is_uninstantiated_template;
    size_t& m_current_index;
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
    void renameVLASizeVars(clang::QualType T, clang::ASTContext &Ctx);
};

// ─── InlineVisitor ─────────────────────────────────────────────────

/// Inlines a specific callee into a caller at a given call site,
/// guided by an oracle file.
class InlineVisitor : public clang::RecursiveASTVisitor<InlineVisitor> {
  public:
    InlineVisitor(clang::ASTContext& ctx, const std::string& caller, const std::string& callee,
                  std::vector<size_t>& call_sites, size_t& current_index,
                  clang::Rewriter& rewriter);

    bool VisitFunctionDecl(clang::FunctionDecl* func);
    bool VisitCallExpr(clang::CallExpr* call);
    clang::SourceLocation findInsertionLoc(const clang::CallExpr* call);
    bool isTopLevelCall(const clang::CallExpr* call);
    bool VisitCXXConstructExpr(clang::CXXConstructExpr* expr);
    bool shouldVisitImplicitCode() const;

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
