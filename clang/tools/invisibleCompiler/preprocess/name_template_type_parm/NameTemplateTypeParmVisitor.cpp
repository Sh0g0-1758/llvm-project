#include "preprocess/name_template_type_parm/NameTemplateTypeParmVisitor.h"
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

// ─── NameTemplateTypeParmVisitor ──────────────────────────────────────────────

NameTemplateTypeParmVisitor::NameTemplateTypeParmVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_index(0) { }

bool NameTemplateTypeParmVisitor::VisitTemplateTypeParmDecl(clang::TemplateTypeParmDecl* decl) {
    if (decl->getIdentifier() != nullptr) return true;

    std::string name = " __type_param__" + std::to_string(m_index++);
    clang::SourceLocation kw = decl->getBeginLoc();
    if (kw.isInvalid()) return true;

    clang::SourceLocation insertAfter = kw;
    // if template type is variadic, insert the type name after the pack token
    if (decl->isParameterPack()) {
        clang::SourceLocation tok = clang::Lexer::findNextToken(kw, m_ctx.getSourceManager(), m_ctx.getLangOpts())
                                        .value_or(clang::Token()).getLocation();
        if (tok.isValid()) insertAfter = tok;
    }

    m_rewriter.InsertTextAfterToken(insertAfter, name);
    return true;
}

bool NameTemplateTypeParmVisitor::VisitNonTypeTemplateParmDecl(clang::NonTypeTemplateParmDecl* decl) {
    if (decl->getIdentifier() != nullptr) return true;

    std::string name = " __non_type_param__" + std::to_string(m_index++) + " ";

    clang::SourceLocation nameLoc = decl->getLocation();
    if (nameLoc.isInvalid()) return true;

    m_rewriter.InsertTextBefore(nameLoc, name);
    return true;
}

}  // namespace invisible
