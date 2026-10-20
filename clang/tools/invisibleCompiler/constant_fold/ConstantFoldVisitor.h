#pragma once

#include "helpers.h"
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

// --- ConstantFoldVisitor -------------------------------------------
// constant-fold: replaces a constant expression of a numeric type with its value, keeping its
// type, eg. `2 * 8 + 1` with `17` and `char('$')` with `(char)36`, see docs/DESIGN.md section 5.13.
class ConstantFoldVisitor : public LaterPassVisitor<ConstantFoldVisitor> {
private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    // the expression last folded: what is inside it isn't folded in the same iteration, as its
    // text is replaced already, eg. `1 + 1` of `(1 + 1) * 3`
    clang::SourceLocation m_last_update_loc_begin;
    clang::SourceLocation m_last_update_loc_end;
    // the `noexcept(...)` of the functions visited so far
    std::vector<clang::SourceRange> m_exception_specs;
    // whether @p expr is the operand of `__typeof__(...)` or the object of a member access, eg.
    // `store[1].b` of a const array, where its value isn't what is written
    bool skipIfBadParent(clang::Expr* expr);
    // whether @p inner is a literal already, eg. `5`, `'a'` or `"s"`, which needn't be folded
    bool isConstantLiteral(clang::Expr* inner);

public:
    ConstantFoldVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    // `2 * 8 + 1` => `17`, see the definition
    virtual bool VisitExpr(clang::Expr* expr);
    // notes the exception specification of @p func, which isn't folded
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    // a default argument is folded where it is written, not where it is inherited
    bool TraverseParmVarDecl(clang::ParmVarDecl* param);
    // whether this iteration folded anything, so that the pass runs again
    bool didFold();
};

// The `constant-fold` mode: folds @p source until nothing changes, or what is folded doesn't
// compile, setting @p did_change and @p is_compilable.
std::string constant_fold(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change = nullptr, bool* is_compilable = nullptr);


}  // namespace invisible
