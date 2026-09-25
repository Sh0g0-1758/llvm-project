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

std::string copyPropagation(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change = nullptr, bool* is_compilable = nullptr);


}  // namespace invisible
