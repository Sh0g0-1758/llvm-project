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
    // the parameter of a requires expression is only seen within its requirements
    if (llvm::isa<clang::RequiresExprBodyDecl>(decl->getDeclContext())) return true;

    std::string canonicalizedName = "___var__" + std::to_string(uniqueIndx++);
    decl->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(canonicalizedName)));
    return true;
}

bool CanonicalizeVarDeclVisitor::VisitParmVarDecl(clang::ParmVarDecl* decl) {
    if (llvm::isa<clang::RequiresExprBodyDecl>(decl->getDeclContext())) return true;
    std::string canonicalizedName = "___var__" + std::to_string(uniqueIndx++);
    decl->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(canonicalizedName)));
    return true;
}

}  // namespace invisible
