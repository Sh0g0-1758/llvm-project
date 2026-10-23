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

// --- CanonicalizeVarDeclVisitor ----------------------------------------------

CanonicalizeVarDeclVisitor::CanonicalizeVarDeclVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_next_index(0) { }

bool CanonicalizeVarDeclVisitor::shouldVisitTemplateInstantiations() const { return true; }

// Renames a local variable or a parameter to `___var__N`, a name unique in the TU, eg. `int y = x * x;`
// gives `int ___var__N = ...;`. The inliner pastes a callee's body into its caller, where a local of the
// callee could clash with or hide one of the caller. The name is changed in the AST, so that every
// use of the variable prints the new name too.
bool CanonicalizeVarDeclVisitor::VisitVarDecl(clang::VarDecl* decl) {
    // a global keeps its name, also one declared inside a function, eg. `extern int count;` in
    // `void f() { extern int count; ++count; }`, which refers to the global `count`
    if (!decl->isLocalVarDeclOrParm() || decl->isLocalExternDecl()) return true;
    // a structured binding, `auto [a, b] = p;`, is a declaration without a name. If renamed, it
    // would be printed as an ordinary variable, `auto ___var__N = p;`, and `a` and `b` would be lost
    if (llvm::isa<clang::DecompositionDecl>(decl)) return true;
    // a parameter of a requires expression, `t` of `requires (T t) { t.size(); }`, is only used
    // inside it, so it can't clash
    if (llvm::isa<clang::RequiresExprBodyDecl>(decl->getDeclContext())) return true;

    std::string canonicalizedName = "___var__" + std::to_string(m_next_index++);
    decl->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(canonicalizedName)));
    return true;
}

}  // namespace invisible
