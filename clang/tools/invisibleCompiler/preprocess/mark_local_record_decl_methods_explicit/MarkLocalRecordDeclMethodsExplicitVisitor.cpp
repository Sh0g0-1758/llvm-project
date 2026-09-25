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

// ─── MarkLocalRecordDeclMethodsExplicitVisitor ──────────────────────────────────────────────

MarkLocalRecordDeclMethodsExplicitVisitor::MarkLocalRecordDeclMethodsExplicitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) { }

bool MarkLocalRecordDeclMethodsExplicitVisitor::shouldVisitTemplateInstantiations() const {
    return true;
}

bool MarkLocalRecordDeclMethodsExplicitVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!isInsideFunction(func)) return true;
    if (!func->isCXXClassMember()) return true;

    if (func->getTemplateSpecializationKind() == clang::TSK_ImplicitInstantiation) {
        func->setTemplateSpecializationKind(clang::TSK_ExplicitSpecialization, func->getPointOfInstantiation());
    }
    return true;
}

}  // namespace invisible
