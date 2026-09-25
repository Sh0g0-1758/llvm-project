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

std::string constant_fold(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change = nullptr, bool* is_compilable = nullptr);


}  // namespace invisible
