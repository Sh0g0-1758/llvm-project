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

// --- BreakLocalRecordDeclStmtVisitor ----------------------------------------------

BreakLocalRecordDeclStmtVisitor::BreakLocalRecordDeclStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) { }

bool BreakLocalRecordDeclStmtVisitor::shouldVisitTemplateInstantiations() const {
    return true;
}

// decl-before-def prints the body of an instantiation, eg. `f<int>`, with clang's printer. When a
// local class is declared together with variables,
//
//     template <class T> void f() { struct P { T x; } a, b; }
//
// the printer writes the variables of `f<int>` but not the class's definition, so the printed body
// uses `P` without defining it. So the class is moved into its own DeclStmt just before the
// variables, and the printer writes it in full:
//
//     struct P { int x; };
//     P a, b;
//
// This is only done when the declaration is directly in a block, eg. not in a for's init.
bool BreakLocalRecordDeclStmtVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl* record_decl) {
    clang::FunctionDecl* enclosing_func = record_decl->isLocalClass();
    if (!enclosing_func) {
        return true;
    }
    if (enclosing_func->getTemplateSpecializationKind() != clang::TSK_ImplicitInstantiation) {
        return true;
    }

    // the DeclStmt that declares the class, with the variables
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

    // the block it is directly in
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

    // what holds the block, unless it is the function's body: a statement, eg. an if or another block
    clang::Stmt* holder = nullptr;
    if (enclosing_func->getBody() != compound) {
        auto parents = m_ctx.getParents(*compound);
        holder = parents.empty() ? nullptr : const_cast<clang::Stmt*>(parents[0].get<clang::Stmt>());
        // clang's parent map was built before any block was rebuilt. If this block was already
        // replaced for another class, it is no longer among its holder's children, and this class
        // is left unsplit.
        if (!holder || !llvm::is_contained(holder->children(), compound)) {
            return true;
        }
    }

    // the variables stay in the DeclStmt, and the class gets a new DeclStmt before it
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

    // a CompoundStmt can't grow, so the block is rebuilt with the new DeclStmt added
    llvm::SmallVector<clang::Stmt*, 16> new_stmts;
    for (clang::Stmt* s : compound->body()) {
        if (s == decl_stmt) {
            new_stmts.push_back(record_stmt);
        }
        new_stmts.push_back(s);
    }

    // the floating point options of the block, eg. of `#pragma STDC FENV_ACCESS`, carry over
    clang::FPOptionsOverride fp_features = compound->hasStoredFPFeatures()
        ? compound->getStoredFPFeatures()
        : clang::FPOptionsOverride();

    clang::CompoundStmt* new_compound = clang::CompoundStmt::Create(
        m_ctx, new_stmts, fp_features,
        compound->getLBracLoc(), compound->getRBracLoc());

    // the new block takes the old one's place
    if (holder) {
        for (clang::Stmt*& child : holder->children()) {
            if (child == compound) {
                child = new_compound;
            }
        }
    } else {
        enclosing_func->setBody(new_compound);
    }
    return true;
}

}  // namespace invisible
