#include "preprocess/access_specifier/AccessSpecifierVisitor.h"
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

// ─── AccessSpecifierVisitor ──────────────────────────────────────────────

AccessSpecifierVisitor::AccessSpecifierVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) { }

bool AccessSpecifierVisitor::VisitAccessSpecDecl(clang::AccessSpecDecl* access_decl) {
    if (access_decl->getAccess() == clang::AS_public) return true;
    m_rewriter.ReplaceText(access_decl->getSourceRange(), "public:");
    return true;
}

bool AccessSpecifierVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl* record_decl) {
    if (!record_decl->isThisDeclarationADefinition()) return true;
    if (record_decl->isLambda()) return true;
    if (llvm::isa<clang::ClassTemplateSpecializationDecl>(record_decl)) return true;

    clang::SourceLocation brace = record_decl->getBraceRange().getBegin();
    if (brace.isInvalid()) return true;

    m_rewriter.InsertTextAfterToken(brace, "\npublic:");
    return true;
}

}  // namespace invisible
