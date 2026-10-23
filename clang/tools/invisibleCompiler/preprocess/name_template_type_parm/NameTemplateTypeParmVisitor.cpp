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

// --- NameTemplateTypeParmVisitor ----------------------------------------------

NameTemplateTypeParmVisitor::NameTemplateTypeParmVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_index(0) { }

// Names an unnamed template type parameter, eg.
//
//     template <class = void, class...> struct S;   =>   template <class __type_param__0 = void, class... __type_param__1> struct S;
//
// decl-before-def prints declarations of a template's members. In them, clang's printer writes
// the class's name with the template's parameters as arguments. It writes an unnamed parameter as
// `type-parameter-0-0`, which doesn't parse, eg. `void f(S other)` =>
// `void f(S<type-parameter-0-0> other);`.
bool NameTemplateTypeParmVisitor::VisitTemplateTypeParmDecl(clang::TemplateTypeParmDecl* decl) {
    if (decl->getIdentifier() != nullptr) return true;

    std::string name = " __type_param__" + std::to_string(m_index++);
    clang::SourceLocation kw = decl->getBeginLoc();
    if (kw.isInvalid()) return true;

    clang::SourceLocation insertAfter = kw;
    // the name goes after `class` or `typename`, or after the `...` of a pack
    if (decl->isParameterPack()) {
        clang::SourceLocation tok = clang::Lexer::findNextToken(kw, m_ctx.getSourceManager(), m_ctx.getLangOpts())
                                        .value_or(clang::Token()).getLocation();
        if (tok.isValid()) insertAfter = tok;
    }

    m_rewriter.InsertTextAfterToken(insertAfter, name);
    return true;
}

// Names an unnamed non-type template parameter, for the same reason, eg.
// `template <int>` => `template <int __non_type_param__0 >`.
bool NameTemplateTypeParmVisitor::VisitNonTypeTemplateParmDecl(clang::NonTypeTemplateParmDecl* decl) {
    if (decl->getIdentifier() != nullptr) return true;

    std::string name = " __non_type_param__" + std::to_string(m_index++) + " ";

    // for an unnamed parameter, clang's location is where the name would be, after the type
    clang::SourceLocation nameLoc = decl->getLocation();
    if (nameLoc.isInvalid()) return true;

    m_rewriter.InsertTextBefore(nameLoc, name);
    return true;
}

}  // namespace invisible
