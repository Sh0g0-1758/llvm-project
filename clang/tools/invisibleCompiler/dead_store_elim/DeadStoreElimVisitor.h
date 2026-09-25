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

std::string deadStoreElim(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change = nullptr, bool* is_compilable = nullptr);


}  // namespace invisible
