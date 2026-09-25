#include "preprocess/comma_init/CommaInitVisitor.h"
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

// ─── CommaInitVisitor ──────────────────────────────────────────────

CommaInitVisitor::CommaInitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

clang::SourceLocation CommaInitVisitor::findInsertionLoc(clang::DeclStmt* decl_stmt) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*decl_stmt);
    const clang::Stmt* last_stmt = decl_stmt;

    while (true) {
        auto parents = m_ctx.getParents(current);
        if (parents.empty())
            return {};

        const auto& parent = parents[0];
        if (parent.get<clang::CompoundStmt>())
            return last_stmt->getBeginLoc();

        if (const auto* stmt = parent.get<clang::Stmt>())
            last_stmt = stmt;

        current = parent;
    }
}

bool CommaInitVisitor::VisitDeclStmt(clang::DeclStmt* decl_stmt) {
    if (decl_stmt->isSingleDecl()) {
        return true;
    }

    llvm::SmallVector<const clang::VarDecl*, 8> vars;
    for (const clang::Decl* decl : decl_stmt->decls()) {
        if (const auto* var_decl = llvm::dyn_cast<clang::VarDecl>(decl)) {
            vars.push_back(var_decl);
        }
    }

    std::string replacement{};
    llvm::raw_string_ostream out(replacement);

    for (const clang::VarDecl* var_decl : vars) {
        var_decl->print(out, m_policy);
        out << ";\n";
    }
    out.flush();

    clang::SourceLocation nearest_compound_stmt = findInsertionLoc(decl_stmt);
    if (nearest_compound_stmt != decl_stmt->getSourceRange().getBegin()) {
        m_rewriter.InsertTextBefore(nearest_compound_stmt, replacement);
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), ";");
    } else {
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), replacement);
    }
    return true;
}

}  // namespace invisible
