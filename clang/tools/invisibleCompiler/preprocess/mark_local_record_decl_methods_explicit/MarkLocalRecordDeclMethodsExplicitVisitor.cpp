#include "preprocess/mark_local_record_decl_methods_explicit/MarkLocalRecordDeclMethodsExplicitVisitor.h"
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

// --- MarkLocalRecordDeclMethodsExplicitVisitor ----------------------------------------------

MarkLocalRecordDeclMethodsExplicitVisitor::MarkLocalRecordDeclMethodsExplicitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) { }

bool MarkLocalRecordDeclMethodsExplicitVisitor::shouldVisitTemplateInstantiations() const {
    return true;
}

// Marks a member of a local class in an instantiation as an explicit specialization, eg. `get` in
//
//     template <class T> auto f() { struct Box { T v; T get() { return v; } }; return Box{}; }
//
// The printer leaves implicitly instantiated members out of the class it prints, so `f<int>`
// printed by decl-before-def would have `struct Box { int v; };` without `get`. Marked explicit,
// `get` is printed in its class.
bool MarkLocalRecordDeclMethodsExplicitVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!isInsideFunction(func)) return true;
    if (!func->isCXXClassMember()) return true;

    if (func->getTemplateSpecializationKind() == clang::TSK_ImplicitInstantiation) {
        func->setTemplateSpecializationKind(clang::TSK_ExplicitSpecialization, func->getPointOfInstantiation());
    }
    return true;
}

}  // namespace invisible
