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

// ─── DeclBeforeDefPrinterHelper ───────────────────────────────────────────

/// Custom PrinterHelper to print function definitions keeping 
/// inline assembly intact
class DeclBeforeDefPrinterHelper : public clang::PrinterHelper {
  public:
    DeclBeforeDefPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy);

    bool handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) override;

  private:
    clang::ASTContext& m_ctx;
    clang::PrintingPolicy m_policy;
};

// ─── DeclBeforeDefVisitor ──────────────────────────────────────────────

/// Puts all function declarations before all function definitions 
class DeclBeforeDefVisitor : public clang::RecursiveASTVisitor<DeclBeforeDefVisitor> {
  public:
    DeclBeforeDefVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    bool VisitVarDecl(clang::VarDecl* decl);
    bool VisitEnumDecl(clang::EnumDecl* enum_decl);
    bool VisitStaticAssertDecl(clang::StaticAssertDecl* static_assert_decl);
    bool VisitTypedefDecl(clang::TypedefDecl* typdef_decl); 
    bool VisitTypeAliasDecl(clang::TypeAliasDecl* using_decl);
    bool VisitFieldDecl(clang::FieldDecl* field_decl);
    std::string getDeferredDefinitions();
    bool shouldVisitTemplateInstantiations() const;
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    clang::PrintingPolicy m_deferred_policy_def;
    clang::PrintingPolicy m_deferred_policy_decl;
    clang::PrintingPolicy m_inplace_policy;
    DeclBeforeDefPrinterHelper m_custom_printer;
    std::vector<std::string> m_deferred_definitions;
    std::map<clang::SourceLocation, std::pair<std::string, clang::SourceLocation>> m_deferred_globals;
    std::map<clang::SourceLocation, std::pair<std::string, clang::SourceLocation>> m_deferred_typedefs;
    void fixParameterPack(std::string& str, clang::FunctionDecl* func);
    unsigned countEnclosingTemplateLevels(const clang::FunctionDecl* func_decl);
    void stripRecordDeclOnlySpecifiers(std::string& s);
};

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

// ─── CanonicalizeVarDeclVisitor ──────────────────────────────────────

/// Canonicalize all local / global variables defined in a translation unit. 
/// The trick here is to make the changes within the AST so that the changes 
/// propagate to the DeclRefExpr. 
class CanonicalizeVarDeclVisitor : public clang::RecursiveASTVisitor<CanonicalizeVarDeclVisitor> {
  public:
    CanonicalizeVarDeclVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitVarDecl(clang::VarDecl* decl);
    bool VisitParmVarDecl(clang::ParmVarDecl* decl);
    bool shouldVisitTemplateInstantiations() const;
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    size_t uniqueIndx;
    std::unordered_map<clang::VarDecl*, std::string> m_var_renames;
};

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

// ─── MarkSwitchCaseVisitor ──────────────────────────────────────

/// Marks the beginning of every case statement with a semi-colon so
/// that any function call present at the start of the set of statements
/// for case is hoisted as part of the case and not above it. 
class MarkSwitchCaseVisitor : public clang::RecursiveASTVisitor<MarkSwitchCaseVisitor> {
  public:
    MarkSwitchCaseVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitCaseStmt(clang::CaseStmt* case_stmt);
    bool VisitDefaultStmt(clang::DefaultStmt* default_stmt);
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

// ─── CompoundStmtWrapperVisitor ──────────────────────────────────────

/// Wraps statements which may have a declStmt (for/if/switch) with a compound stmt
/// This helps provide a valid space to hoist a comma separated declarator list
class CompoundStmtWrapperVisitor : public clang::RecursiveASTVisitor<CompoundStmtWrapperVisitor> {
  public:
    CompoundStmtWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitForStmt(clang::ForStmt* for_stmt);
    bool VisitIfStmt(clang::IfStmt* if_stmt);
    bool VisitSwitchStmt(clang::SwitchStmt* switch_stmt);
    bool VisitWhileStmt(clang::WhileStmt* while_stmt);
    bool VisitDoStmt(clang::DoStmt* do_stmt);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
};

// ─── ParenExprWrapperVisitor ──────────────────────────────────────

/// Wraps binary operator (,) with a paranthesis
class ParenExprWrapperVisitor : public clang::RecursiveASTVisitor<ParenExprWrapperVisitor> {
  public:
    ParenExprWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitBinaryOperator(clang::BinaryOperator* bin_op);
    bool isWrappedWithParenExpr(const clang::BinaryOperator* binary_op);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
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
    clang::PrintingPolicy m_policy;
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
    void hoist_call_at_nearest_compound_statement(clang::CallExpr* call);
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::SourceLocation m_last_update_loc;
    clang::SourceRange m_last_call_loc;

    clang::SourceLocation findInsertionLoc(const clang::CallExpr* call);
    bool isTopLevelCall(const clang::CallExpr* call);
    bool m_is_uninstantiated_template;
    size_t& m_current_index;
    clang::PrintingPolicy m_policy;
    bool isTransparentParent(const clang::Stmt* s, bool isExplicitCoversionOperator);
    clang::DynTypedNode getSemanticParent(const clang::Stmt* node, bool isExplicitCoversionOperator);
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

// ─── ConstantFoldVisitor ───────────────────────────────────────────

class constantFoldVisitor : public clang::RecursiveASTVisitor<constantFoldVisitor> {
private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::SourceLocation m_last_update_loc_begin;
    clang::SourceLocation m_last_update_loc_end;
    bool skip_if_bad_parent(clang::Expr* expr);
    bool is_constant_literal(clang::Expr* inner);

public:
    constantFoldVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    virtual bool VisitExpr(clang::Expr* expr);
    bool did_fold();
};

// ─── deadCodeElimVisitor ───────────────────────────────────────────

class deadCodeElimVisitor : public clang::RecursiveASTVisitor<deadCodeElimVisitor> {
private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::SourceLocation m_last_update_loc_begin;
    clang::SourceLocation m_last_update_loc_end;
    clang::PrintingPolicy m_policy;

public:
    deadCodeElimVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    virtual bool VisitIfStmt(clang::IfStmt* if_stmt);
    virtual bool VisitForStmt(clang::ForStmt* for_stmt);
    virtual bool VisitWhileStmt(clang::WhileStmt* while_stmt);
    virtual bool VisitDoStmt(clang::DoStmt* do_stmt);
    virtual bool VisitConditionalOperator(clang::ConditionalOperator* conditional_op);
    bool has_label(clang::Stmt* stmt);
    bool did_elim();
};

// ─── deadCodeElimCleanupVisitor ───────────────────────────────────────────

class deadCodeElimCleanupVisitor : public clang::RecursiveASTVisitor<deadCodeElimCleanupVisitor> {
private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    bool did_remove;
    std::vector<std::pair<clang::SourceLocation, clang::SourceLocation>> m_removable_locs;
    bool is_parent_stmtExpr(clang::CompoundStmt* stmt);
    bool is_parent_valid_to_remove_body(clang::CompoundStmt* stmt);

public:
    deadCodeElimCleanupVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    virtual bool VisitCompoundStmt(clang::CompoundStmt* stmt);
    bool did_elim();
    void merge_and_clean_removable_locs();
};

// ─── deadCodeElimCfgAnalysisVisitor ───────────────────────────────────────────

class deadCodeElimCfgAnalysisVisitor : public clang::RecursiveASTVisitor<deadCodeElimCfgAnalysisVisitor> {
private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    clang::CFG::BuildOptions m_cfg_options;
    bool did_remove;
    void extend_unreachable_loc_for_parent(std::vector<std::pair<clang::SourceLocation, clang::SourceLocation>>& unreachable_locs, clang::Stmt* stmt);
    bool has_label(clang::Stmt* stmt);

public:
    deadCodeElimCfgAnalysisVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    virtual bool VisitFunctionDecl(clang::FunctionDecl* func);
    bool did_elim();
};

// ─── copyPropagationVisitor ───────────────────────────────────────────

struct reaching_copies_t {
  bool m_is_top = true;
  bool is_top() const {
    return m_is_top;
  }
  void mark_is_not_top() {
    m_is_top = false;
  }

  std::vector<clang::VarDecl*> m_vars_that_can_alias;
  void add_possible_alias_target(clang::VarDecl* identifier) {
    if(std::find(m_vars_that_can_alias.begin(), m_vars_that_can_alias.end(), identifier) == m_vars_that_can_alias.end()) {
      m_vars_that_can_alias.push_back(identifier);
    }
  }
  #define in_list_of_vars_that_can_alias(alias_list, var) \
    (std::find(alias_list.begin(), alias_list.end(), var) != alias_list.end())

  void merge_vars_that_can_alias(const reaching_copies_t& other) {
    for(const auto& it : other.m_vars_that_can_alias) {
      add_possible_alias_target(it);
    }
  }

  struct copy_target_t {
    clang::VarDecl* m_identifier;
    clang::APValue m_numeric_constant;
    bool m_has_storage;

    bool is_same(const copy_target_t& other) const {
      if (this->m_has_storage ^ other.m_has_storage) return false;

      if (this->m_has_storage) return this->m_identifier == other.m_identifier;
      else {
        llvm::FoldingSetNodeID IDA, IDB;
        this->m_numeric_constant.Profile(IDA);
        other.m_numeric_constant.Profile(IDB);
        return IDA == IDB;
      }
    }

    std::string get_as_string(clang::ASTContext& ctx, clang::QualType type, clang::PrintingPolicy& policy) {
      if (this->m_has_storage) return m_identifier->getNameAsString();
      else {
        if (type->isBooleanType()) {
          return m_numeric_constant.getInt().getBoolValue() ? "1" : "0";
        }

        std::string val_as_string{};
        llvm::raw_string_ostream out(val_as_string);
        m_numeric_constant.printPretty(out, policy, type, &ctx);
        out.flush();
        return val_as_string;
      }
    }
  };

  struct copy_t {
    clang::VarDecl* m_identifier;
    copy_target_t m_to_copy;
    std::string get_name() const {
      return m_identifier->getNameAsString();
    }
    bool is_different_from(const copy_t& other) const {
      return this->m_identifier != other.m_identifier;
    }
    bool intersects_with(const copy_t& other) const {
      return (this->m_identifier == other.m_identifier) &&
             !(this->m_to_copy.is_same(other.m_to_copy));
    }
    bool is_same_as(const copy_t& other) const {
      return (this->m_identifier == other.m_identifier) &&
             (this->m_to_copy.is_same(other.m_to_copy));
    }
    bool has_global_storage() const {
      return (m_identifier->hasGlobalStorage()) ||
             (m_to_copy.m_has_storage && m_to_copy.m_identifier->hasGlobalStorage());
    }
    bool can_alias(const std::vector<clang::VarDecl*>& alias_list) const {
      return in_list_of_vars_that_can_alias(alias_list, m_identifier) || 
             (m_to_copy.m_has_storage && in_list_of_vars_that_can_alias(alias_list, m_to_copy.m_identifier));
    }
    bool uses_identifier(clang::VarDecl* identifier) const {
      return (identifier == m_identifier) || 
             (m_to_copy.m_has_storage && identifier == m_to_copy.m_identifier);
    }
  };

  std::vector<copy_t> m_copies;

  void add_copies(copy_t&& copy) {
    m_copies.push_back(std::move(copy));
  }

  std::string find_if_copy_exists(clang::VarDecl* identifier, clang::ASTContext& ctx, clang::PrintingPolicy& policy) {
    if (!identifier || m_copies.empty()) return {};

    for(auto& it : m_copies) {
      if(it.m_identifier == identifier) {
        return it.m_to_copy.get_as_string(ctx, identifier->getType(), policy);
      }
    }
    return {};
  }

  bool empty() const {
    return m_copies.empty() && m_vars_that_can_alias.empty();
  }

  void kill_copies_with_global_storage() {
    if (m_copies.empty()) return;
    auto front = m_copies.begin();
    auto back = m_copies.end();

    while (front != back) {
      if(front->has_global_storage()) {
        std::swap((*front), *(--back));
      } else {
        front++;
      }
    }

    m_copies.erase(back, m_copies.end());
  }

  void kill_copies_that_can_alias() {
    if (m_copies.empty()) return;
    auto front = m_copies.begin();
    auto back = m_copies.end();

    while (front != back) {
      if(front->can_alias(this->m_vars_that_can_alias)) {
        std::swap((*front), *(--back));
      } else {
        front++;
      }
    }

    m_copies.erase(back, m_copies.end());
  }

  void kill_copies_that_use_identifier(clang::VarDecl* identifier) {
    if (m_copies.empty()) return;
    auto front = m_copies.begin();
    auto back = m_copies.end();

    while (front != back) {
      if(front->uses_identifier(identifier)) {
        std::swap((*front), *(--back));
      } else {
        front++;
      }
    }

    m_copies.erase(back, m_copies.end());
  }

  bool operator==(const reaching_copies_t& other) const {
      if (m_is_top != other.m_is_top) return false;
      if (m_vars_that_can_alias.size() != other.m_vars_that_can_alias.size()) return false;
      if (m_copies.size() != other.m_copies.size()) return false;

      auto alias_a = m_vars_that_can_alias, alias_b = other.m_vars_that_can_alias;
      std::sort(alias_a.begin(), alias_a.end());
      std::sort(alias_b.begin(), alias_b.end());
      if (alias_a != alias_b) return false;

      std::vector<bool> matched(other.m_copies.size(), false);
      for (const auto& a : m_copies) {
          bool found = false;
          for (size_t i = 0; i < other.m_copies.size(); i++) {
              if (!matched[i] && a.is_same_as(other.m_copies[i])) {
                  matched[i] = true;
                  found = true;
                  break;
              }
          }
          if (!found) return false;
      }
      return true;
  }

  bool operator!=(const reaching_copies_t& other) const {
    return !(*this == other);
  }
};

class transferStmtVisitor : public clang::RecursiveASTVisitor<transferStmtVisitor> {
private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    reaching_copies_t& m_copies;
    clang::VarDecl* extract_var_decl(clang::Expr* expr);
    bool is_dereference_operation(clang::Expr* expr);
    void possibly_add_viable_copy(clang::VarDecl* var_decl, clang::Expr* expr);
    bool is_primitive_type(clang::QualType qt);
    void transfer_vars_in_type(clang::QualType T);
    bool m_do_updates;
    bool& m_did_propagate;
    // So there can be instances when the clangCFG Block has multiple
    // CFGElems, some of which are children of their peer CFGElems. 
    // This can lead to multiple updates at the same place for the
    // last statement of a CFGBlock. This is to guard against that. 
    std::unordered_map<unsigned, bool>& m_source_loc_updated;

public:
    transferStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, clang::PrintingPolicy policy, reaching_copies_t& copies, bool do_updates, bool& did_propagate, std::unordered_map<unsigned, bool>& source_loc_updated);
    bool VisitDeclStmt(clang::DeclStmt* decl_stmt);
    virtual bool VisitBinaryOperator(clang::BinaryOperator* binary_op);
    virtual bool VisitUnaryOperator(clang::UnaryOperator* unary_op);
    virtual bool VisitCallExpr(clang::CallExpr* _call);
    virtual bool VisitImplicitCastExpr(clang::ImplicitCastExpr* ice);
    bool VisitGCCAsmStmt(clang::GCCAsmStmt* asm_stmt);
    bool VisitArraySubscriptExpr(clang::ArraySubscriptExpr* ase);
    virtual bool shouldTraversePostOrder();
    virtual bool TraverseStmtExpr(clang::StmtExpr* stmt_expr);
    virtual bool TraverseBinaryOperator(clang::BinaryOperator* bin_op);
};

class copyPropagationVisitor : public clang::RecursiveASTVisitor<copyPropagationVisitor> {
private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    clang::CFG::BuildOptions m_cfg_options;
    bool m_did_propagate;
    void meet(reaching_copies_t& merge_into, const reaching_copies_t& merge_from);
    void transfer(reaching_copies_t& copies, clang::Stmt* stmt, bool do_updates, std::unordered_map<unsigned, bool>& source_loc_updated);

public:
    copyPropagationVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    virtual bool VisitFunctionDecl(clang::FunctionDecl* func);
    bool did_propagate();
};

// ─── deadStoreElimVisitor ───────────────────────────────────────────

struct live_variables_t {
  bool m_is_bottom = true;
  std::unordered_map<clang::VarDecl*, bool> live_set;
  std::unordered_map<clang::VarDecl*, bool> has_usage_set;

  bool is_bottom() const {
    return m_is_bottom;
  }
  void mark_is_not_bottom() {
    m_is_bottom = false;
  }

  void mark_as_live(clang::VarDecl* identifier) {
    live_set[identifier] = true;
  }
  void mark_as_dead(clang::VarDecl* identifier) {
    // erasing an element that does not exist in the map is safe. 
    live_set.erase(identifier);
  }

  void record_usage(clang::VarDecl* identifier) {
    has_usage_set[identifier] = true;
  }

  void merge(const live_variables_t& other) {
    for(const auto& it : other.live_set) {
      this->live_set.insert(it);
    }
    for(const auto& it : other.has_usage_set) {
      this->has_usage_set.insert(it);
    }
  }

  bool is_live(clang::VarDecl* identifier) const {
    if (live_set.find(identifier) != live_set.end()) return true;
    return false;
  }

  bool has_usage(clang::VarDecl* identifier) const {
    if (has_usage_set.find(identifier) != has_usage_set.end()) return true;
    return false;
  }

  bool is_empty() const {
    return live_set.empty() && has_usage_set.empty();
  }

  bool operator==(const live_variables_t& other) const {
    if (m_is_bottom != other.m_is_bottom) return false;
    if (live_set.size() != other.live_set.size()) return false;
    if (has_usage_set.size() != other.has_usage_set.size()) return false;

    for(const auto& it : other.live_set) {
      if (live_set.find(it.first) == live_set.end()) return false;
    }

    for(const auto& it : other.has_usage_set) {
      if (has_usage_set.find(it.first) == has_usage_set.end()) return false;
    }

    return true;
  }

  bool operator!=(const live_variables_t& other) const {
    return !(*this == other);
  }
};

class transferLivenessStmtVisitor : public clang::RecursiveASTVisitor<transferLivenessStmtVisitor> {
private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    live_variables_t& m_live_variables;
    clang::VarDecl* extract_var_decl(clang::Expr* expr);
    bool is_primitive_type(clang::QualType qt);
    void recurse_through_type(clang::QualType T);
    bool m_do_updates;
    bool& m_did_elim;
    // So there can be instances when the clangCFG Block has multiple
    // CFGElems, some of which are children of their peer CFGElems. 
    // This can lead to multiple updates at the same place for the
    // last statement of a CFGBlock. This is to guard against that. 
    std::unordered_map<unsigned, bool>& m_source_loc_updated;
    std::unordered_map<clang::VarDecl*, bool>& m_aliased_and_global_vars;
    void mark_aliased_and_global_vars_as_live();
    bool is_parent_an_assignment_op(clang::Expr* expr);

public:
    transferLivenessStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, clang::PrintingPolicy policy, live_variables_t& live_variables, bool do_updates, bool& did_elim, std::unordered_map<unsigned, bool>& source_loc_updated, std::unordered_map<clang::VarDecl*, bool>& aliased_and_global_vars);
    bool VisitDeclStmt(clang::DeclStmt* decl_stmt);
    virtual bool VisitBinaryOperator(clang::BinaryOperator* binary_op);
    virtual bool VisitUnaryOperator(clang::UnaryOperator* unary_op);
    virtual bool VisitCallExpr(clang::CallExpr* _call);
    virtual bool VisitImplicitCastExpr(clang::ImplicitCastExpr* ice);
    bool VisitArraySubscriptExpr(clang::ArraySubscriptExpr* ase);
    bool VisitGCCAsmStmt(clang::GCCAsmStmt* asm_stmt);
    virtual bool TraverseStmtExpr(clang::StmtExpr* stmt_expr);
    virtual bool TraverseBinaryOperator(clang::BinaryOperator* bin_op);
};

class deadStoreElimVisitor : public clang::RecursiveASTVisitor<deadStoreElimVisitor> {
private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    clang::CFG::BuildOptions m_cfg_options;
    bool m_did_elim;
    void meet(live_variables_t& merge_into, const live_variables_t& merge_from);
    void transfer(live_variables_t& live_vars, clang::Stmt* stmt, bool do_updates, std::unordered_map<unsigned, bool>& source_loc_updated, std::unordered_map<clang::VarDecl*, bool>& aliased_and_global_vars);

public:
    deadStoreElimVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    virtual bool VisitFunctionDecl(clang::FunctionDecl* func);
    bool did_elim();
};

// ─── Free functions ────────────────────────────────────────────────

/// Split comma-separated declarations into individual statements and
/// hoist nested function calls into temporaries.  Returns the
/// transformed source.
std::string preprocess(const std::string& source, const std::string& filename, std::vector<std::string>& args);

std::string constant_fold(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change = nullptr, bool* is_compilable = nullptr);

std::string deadCodeElim(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change = nullptr, bool* is_compilable = nullptr);

std::string copyPropagation(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change = nullptr, bool* is_compilable = nullptr);

std::string deadStoreElim(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change = nullptr, bool* is_compilable = nullptr);

std::string oneForAll(const std::string& source, const std::string& filename, std::vector<std::string>& args);

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
