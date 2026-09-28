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

    // a class or enumeration the declaration defines, eg. `struct S {...} a, b;`, is
    // printed once: by itself if it is named, with the first variable otherwise, which
    // the others are declared by the type of. Other declarations, eg. typedefs, are left.
    const clang::TagDecl* tag = nullptr;
    llvm::SmallVector<const clang::VarDecl*, 8> vars;
    for (const clang::Decl* decl : decl_stmt->decls()) {
        if (const auto* var_decl = llvm::dyn_cast<clang::VarDecl>(decl)) {
            vars.push_back(var_decl);
        } else if (const auto* defined = llvm::dyn_cast<clang::TagDecl>(decl); defined && !tag && vars.empty()) {
            tag = defined;
        } else {
            return true;
        }
    }
    bool anonymous = tag && !tag->getIdentifier();
    if (vars.empty() || (anonymous && llvm::any_of(vars, [&](const clang::VarDecl* var) {
            return var->getType() != vars.front()->getType() || var->getInitStyle() == clang::VarDecl::CallInit;
        })))
        return true;

    std::string replacement{};
    llvm::raw_string_ostream out(replacement);
    clang::PrintingPolicy policy = m_policy;
    if (tag && !anonymous) {
        tag->print(out, policy);
        out << ";\n";
        policy.IncludeTagDefinition = false;
    }
    for (const clang::VarDecl* var_decl : vars) {
        if (anonymous && var_decl != vars.front()) {
            out << "__typeof__(" << vars.front()->getName() << ") " << var_decl->getName();
            if (const clang::Expr* init = var_decl->getInit()) {
                if (var_decl->getInitStyle() == clang::VarDecl::CInit) out << " = ";
                init->printPretty(out, nullptr, policy);
            }
        } else {
            var_decl->print(out, policy);
        }
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
