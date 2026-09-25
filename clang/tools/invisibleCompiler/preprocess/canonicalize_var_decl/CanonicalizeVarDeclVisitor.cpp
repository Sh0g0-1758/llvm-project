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
    // classes can have static member variables which should not be renamed. 
    // This is because this renaming does not really propagate to the site of
    // usage. It does for normal VarDecls but not for a static data member of a class. 
    if (decl->isStaticDataMember()) return true;
    // we also do not rename templatized variables because their specializations
    // would also need to be renamed then and well first we would need to somehow
    // keep track of that and second, some cleanup relating to template specialization
    // would also be needed at the place of the template global variable usage. 
    if (decl->getDescribedVarTemplate()) return true;
    if (llvm::isa<clang::VarTemplateSpecializationDecl>(decl)) return true;


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
