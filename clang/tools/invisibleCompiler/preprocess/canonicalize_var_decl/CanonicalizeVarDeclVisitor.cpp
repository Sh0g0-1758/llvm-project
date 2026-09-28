#include "preprocess/canonicalize_var_decl/CanonicalizeVarDeclVisitor.h"
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

// ─── CanonicalizeVarDeclVisitor ──────────────────────────────────────────────

CanonicalizeVarDeclVisitor::CanonicalizeVarDeclVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), uniqueIndx(0) { }

bool CanonicalizeVarDeclVisitor::shouldVisitTemplateInstantiations() const { return true; }

bool CanonicalizeVarDeclVisitor::VisitVarDecl(clang::VarDecl* decl) {
    // only locals are renamed, what they could clash with once a function is inlined
    // into another. A block scope `extern` declares a global.
    if (!decl->isLocalVarDeclOrParm() || decl->isLocalExternDecl()) return true;
    // a structured binding declaration is unnamed, it is printed as a variable once named
    if (llvm::isa<clang::DecompositionDecl>(decl)) return true;


    // Return the first declaration of this declaration or itself if this
    // is the only declaration.
    std::string canonicalizedName;
    clang::VarDecl* canon_decl = decl->getCanonicalDecl();
    if (m_var_renames.find(canon_decl) == m_var_renames.end()) {
        canonicalizedName = "___var__" + std::to_string(uniqueIndx++);
        m_var_renames[canon_decl] = canonicalizedName;
    } else {
        canonicalizedName = m_var_renames[canon_decl];
    }
    decl->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(canonicalizedName)));
    return true;
}

bool CanonicalizeVarDeclVisitor::VisitParmVarDecl(clang::ParmVarDecl* decl) {
    std::string canonicalizedName = "___var__" + std::to_string(uniqueIndx++);
    decl->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(canonicalizedName)));
    return true;
}

}  // namespace invisible
