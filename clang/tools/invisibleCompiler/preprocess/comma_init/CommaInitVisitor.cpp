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

// Where the split declarations go: before the statement of the nearest block that holds the
// declaration, eg. the for statement of `for (int i = 0, n = f(); i < n; ++i)`, or the
// declaration itself when it is a statement of the block.
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
        // a body without braces, eg. the for in `if (c) for (int i = 0, n = f(); ...)`: the split
        // declarations go in braces with it, so that they stay inside the if, see isUnbracedBody
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
// hoist-calls puts a call's temporary before the statement holding it; with `b = f(a)` declared
// together with `a`, the temporary would be computed before `a` is initialized. A declaration
// that isn't a statement of a block, eg. a for statement's init, moves before the statement,
// which compound-stmt-wrapper wrapped in a block of its own, and leaves `;` in its place.
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
    // the variables of an unnamed class, `struct {...} a = {1}, b;`, are declared by the type of
    // the first, `__typeof__(a) b;`, which needs them all of one type and none written `T x(...)`
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
        const clang::SourceManager& src_mgr = m_ctx.getSourceManager();
        clang::SourceLocation end = clang::Lexer::findLocationAfterToken(m_unbraced->getEndLoc(), clang::tok::semi, src_mgr,
                                                                         m_ctx.getLangOpts(), false);
        if (end.isInvalid()) end = clang::Lexer::getLocForEndOfToken(m_unbraced->getEndLoc(), 0, src_mgr, m_ctx.getLangOpts());
        m_rewriter.InsertTextBefore(nearest_compound_stmt, "{\n" + replacement);
        m_rewriter.InsertTextAfter(end, "\n}");
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), ";");
    } else if (nearest_compound_stmt != decl_stmt->getSourceRange().getBegin()) {
        m_rewriter.InsertTextBefore(nearest_compound_stmt, replacement);
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), ";");
    } else {
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), replacement);
    }
    return true;
}

}  // namespace invisible
