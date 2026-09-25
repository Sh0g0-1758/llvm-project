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

std::string deadCodeElim(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change = nullptr, bool* is_compilable = nullptr);


}  // namespace invisible
