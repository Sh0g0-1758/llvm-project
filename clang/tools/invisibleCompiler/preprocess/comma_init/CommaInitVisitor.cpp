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

// --- CommaInitVisitor ----------------------------------------------

CommaInitVisitor::CommaInitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(codePolicy(ctx)) {
    
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
}

// Where the split declarations go: the start of the statement that holds @p decl_stmt and is
// directly in a block, eg. the for statement of `for (int i = 0, n = f(); i < n; ++i)`. When
// @p decl_stmt is directly in a block, that is @p decl_stmt itself.
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
        // a body without braces, eg. the for in `if (c) for (int i = 0, n = f(); ...)`: new braces
        // hold the declarations and the for, so that they stay inside the if, see isUnbracedBody
        if (const auto* parent_stmt = parent.get<clang::Stmt>(); parent_stmt && isUnbracedBody(parent_stmt, last_stmt)) {
            m_unbraced = last_stmt;
            return last_stmt->getBeginLoc();
        }

        if (const auto* stmt = parent.get<clang::Stmt>())
            last_stmt = stmt;

        current = parent;
    }
}

// Splits a declaration of several variables into one declaration each, eg.
//
//     int a = 1, b = f(a);                 =>   int a = 1;
//                                               int b = f(a);
//     for (int i = 0, n = f(); i < n; ++i) =>   int i = 0;
//                                               int n = f();
//                                               for (; i < n; ++i)
//
// hoist-calls puts a call's temporary before the whole statement. In `int a = 1, b = f(a);` the
// temporary of `f(a)` would then be computed before `a` is set.
bool CommaInitVisitor::VisitDeclStmt(clang::DeclStmt* decl_stmt) {
    if (decl_stmt->isSingleDecl()) {
        return true;
    }

    // a class or enumeration the declaration defines, eg. `struct S {...} a, b;`, is printed
    // once: as a separate declaration if it is named, else with the first variable. A
    // declaration with anything else in it, eg. a typedef, is left as it is.
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
    // an unnamed class has no name to declare the other variables with, so they take the first
    // one's type: `struct {...} a = {1}, b;` => `struct {...} a = {1};` and `__typeof__(a) b;`.
    // That is only done when all have the same type and none is initialized as `x(...)`.
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

    m_unbraced = nullptr;
    clang::SourceLocation nearest_compound_stmt = findInsertionLoc(decl_stmt);
    if (m_unbraced) {
        // new braces around the declarations and the unbraced body
        const clang::SourceManager& src_mgr = m_ctx.getSourceManager();
        clang::SourceLocation end = clang::Lexer::findLocationAfterToken(m_unbraced->getEndLoc(), clang::tok::semi, src_mgr,
                                                                         m_ctx.getLangOpts(), false);
        if (end.isInvalid()) end = clang::Lexer::getLocForEndOfToken(m_unbraced->getEndLoc(), 0, src_mgr, m_ctx.getLangOpts());
        m_rewriter.InsertTextBefore(nearest_compound_stmt, "{\n" + replacement);
        m_rewriter.InsertTextAfter(end, "\n}");
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), ";");
    } else if (nearest_compound_stmt != decl_stmt->getSourceRange().getBegin()) {
        // not directly in a block, eg. a for's init: the declarations go before the for, and `;`
        // is left in the init. compound-stmt-wrapper has put the for in braces, so the variables
        // stay scoped to it.
        m_rewriter.InsertTextBefore(nearest_compound_stmt, replacement);
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), ";");
    } else {
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), replacement);
    }
    return true;
}

}  // namespace invisible
