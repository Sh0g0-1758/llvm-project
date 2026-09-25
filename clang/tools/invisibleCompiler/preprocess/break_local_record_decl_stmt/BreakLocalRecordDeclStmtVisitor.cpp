#include "preprocess/break_local_record_decl_stmt/BreakLocalRecordDeclStmtVisitor.h"
#include "helpers.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/JSON.h"
#include "clang/AST/TemplateBase.h"

#include <string>
#include <queue>
#include <stack>

namespace invisible {

// ─── BreakLocalRecordDeclStmtVisitor ──────────────────────────────────────────────

BreakLocalRecordDeclStmtVisitor::BreakLocalRecordDeclStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) { }

bool BreakLocalRecordDeclStmtVisitor::shouldVisitTemplateInstantiations() const {
    return true;
}

// This assumes that the parent of a LocalRecordDecl whose DeclStmt has multiple decls
// is a compoundStmt. Because only then can it break the declStmt into two and append
// it back. 
bool BreakLocalRecordDeclStmtVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl* record_decl) {
    clang::FunctionDecl* enclosing_func = record_decl->isLocalClass();
    if (!enclosing_func) {
        return true;
    }
    if (enclosing_func->getTemplateSpecializationKind() != clang::TSK_ImplicitInstantiation) {
        return true;
    }

    const clang::DeclStmt* found_decl_stmt = nullptr;
    for (const auto& parent : m_ctx.getParents(*record_decl)) {
        if (const auto* ds = parent.get<clang::DeclStmt>()) {
            found_decl_stmt = ds;
            break;
        }
    }
    if (!found_decl_stmt || found_decl_stmt->isSingleDecl()) {
        return true;
    }

    clang::DeclStmt* decl_stmt = const_cast<clang::DeclStmt*>(found_decl_stmt);

    const clang::CompoundStmt* found_compound = nullptr;
    for (const auto& parent : m_ctx.getParents(*decl_stmt)) {
        if (const auto* cs = parent.get<clang::CompoundStmt>()) {
            found_compound = cs;
            break;
        }
    }
    if (!found_compound) {
        return true;
    }
    clang::CompoundStmt* compound = const_cast<clang::CompoundStmt*>(found_compound);

    llvm::SmallVector<clang::Decl*, 4> remaining_decls;
    for (clang::Decl* d : decl_stmt->getDeclGroup()) {
        if (d != record_decl) {
            remaining_decls.push_back(d);
        }
    }
    decl_stmt->setDeclGroup(clang::DeclGroupRef::Create(m_ctx, remaining_decls.data(), remaining_decls.size()));

    clang::DeclGroupRef record_group(record_decl);
    clang::DeclStmt* record_stmt = new (m_ctx) clang::DeclStmt(
        record_group, record_decl->getBeginLoc(), record_decl->getEndLoc());

    llvm::SmallVector<clang::Stmt*, 16> new_stmts;
    for (clang::Stmt* s : compound->body()) {
        if (s == decl_stmt) {
            new_stmts.push_back(record_stmt);
        }
        new_stmts.push_back(s);
    }

    clang::FPOptionsOverride fp_features = compound->hasStoredFPFeatures()
        ? compound->getStoredFPFeatures()
        : clang::FPOptionsOverride();

    clang::CompoundStmt* new_compound = clang::CompoundStmt::Create(
        m_ctx, new_stmts, fp_features,
        compound->getLBracLoc(), compound->getRBracLoc());

    enclosing_func->setBody(new_compound);
    return true;
}

}  // namespace invisible
