#include "preprocess/rename_global_var_refs/RenameGlobalVarRefsVisitor.h"
#include "helpers.h"

#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

namespace invisible {

// ─── RenameGlobalVarRefsVisitor ──────────────────────────────────────

RenameGlobalVarRefsVisitor::RenameGlobalVarRefsVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) { }

// function declarations are re-printed from the AST by DeclBeforeDefVisitor,
// which already carries the canonicalized names.
bool RenameGlobalVarRefsVisitor::TraverseDecl(clang::Decl* decl) {
    // except for the ones DeclBeforeDef leaves as written
    if (llvm::isa_and_nonnull<clang::FunctionDecl, clang::FunctionTemplateDecl>(decl) &&
        !isKeptByDeclBeforeDef(decl->getAsFunction()))
        return true;
    // which RecursiveASTVisitor doesn't traverse
    if (auto* func = llvm::dyn_cast_or_null<clang::FunctionDecl>(decl))
        if (clang::Expr* cond = clang::ExplicitSpecifier::getFromDecl(func).getExpr()) TraverseStmt(cond);
    return clang::RecursiveASTVisitor<RenameGlobalVarRefsVisitor>::TraverseDecl(decl);
}

bool RenameGlobalVarRefsVisitor::VisitUsingDecl(clang::UsingDecl* decl) {
    for (clang::UsingShadowDecl* shadow : decl->shadows()) {
        auto* var = llvm::dyn_cast<clang::VarDecl>(shadow->getTargetDecl());
        if (var && !var->isLocalVarDeclOrParm()) rename(decl->getNameInfo().getLoc(), var);
    }
    return true;
}

// the declared name of a renamed global variable.
bool RenameGlobalVarRefsVisitor::VisitVarDecl(clang::VarDecl* decl) {
    if (!decl->isLocalVarDeclOrParm()) rename(decl->getLocation(), decl);
    return true;
}

bool RenameGlobalVarRefsVisitor::VisitDeclRefExpr(clang::DeclRefExpr* ref) {
    auto* var = llvm::dyn_cast<clang::VarDecl>(ref->getDecl());
    // locals declared within the kept text are spelled with their old names
    // as well, so they stay consistent without any edit.
    if (var && !var->isLocalVarDeclOrParm()) rename(ref->getLocation(), var);
    return true;
}

void RenameGlobalVarRefsVisitor::rename(clang::SourceLocation loc, clang::VarDecl* var) {
    if (loc.isInvalid()) return;
    clang::SourceManager& source_mgr = m_ctx.getSourceManager();
    unsigned length = clang::Lexer::MeasureTokenLength(loc, source_mgr, m_ctx.getLangOpts());
    if (llvm::StringRef(source_mgr.getCharacterData(loc), length) == var->getName()) return;
    m_rewriter.ReplaceText(loc, length, var->getName());
}

}  // namespace invisible
