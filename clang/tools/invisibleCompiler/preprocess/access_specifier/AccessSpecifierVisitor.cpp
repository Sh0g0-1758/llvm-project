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

// --- AccessSpecifierVisitor ----------------------------------------------

AccessSpecifierVisitor::AccessSpecifierVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, bool keep_observable_access)
    : m_ctx(ctx), m_rewriter(rewriter), m_keep_observable_access(keep_observable_access) { }

// Only in the fallback the driver retries with (keep_observable_access), when making everything
// public doesn't compile: a constructor, destructor or assignment operator keeps the access it is
// declared with, eg.
//
//     protected: ~buffer() = default;   =>   public: ... protected: ~buffer() = default; public: ...
//
// as code can tell its access, eg. `static_assert(!std::is_destructible_v<buffer>)` or a SFINAE
// test choosing an overload. The member is put between its own access label and `public:`, the
// members around it staying public. Implicit and out-of-line declarations have no label to keep.
bool AccessSpecifierVisitor::VisitCXXMethodDecl(clang::CXXMethodDecl* method) {
    bool special = llvm::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl>(method) ||
                   method->isCopyAssignmentOperator() || method->isMoveAssignmentOperator();
    if (!m_keep_observable_access || !special || method->isImplicit() || method->getAccess() == clang::AS_public ||
        method->isOutOfLine())
        return true;
    const clang::Decl* decl = method;
    if (const clang::FunctionTemplateDecl* tmpl = method->getDescribedFunctionTemplate()) decl = tmpl;
    const clang::SourceManager& sm = m_ctx.getSourceManager();
    clang::SourceLocation end = clang::Lexer::findLocationAfterToken(decl->getEndLoc(), clang::tok::semi, sm, m_ctx.getLangOpts(), false);
    if (end.isInvalid()) end = clang::Lexer::getLocForEndOfToken(decl->getEndLoc(), 0, sm, m_ctx.getLangOpts());
    m_rewriter.InsertTextBefore(decl->getBeginLoc(), "\n" + clang::getAccessSpelling(method->getAccess()).str() + ":\n");
    m_rewriter.InsertTextAfter(end, "\npublic:\n");
    return true;
}

// `private:` and `protected:` => `public:`. decl-before-def moves member definitions out of their
// class, hoist-calls and the inliner write code that names members from outside it, and the copy
// of a member function is a free function: all of them need public members.
bool AccessSpecifierVisitor::VisitAccessSpecDecl(clang::AccessSpecDecl* access_decl) {
    if (access_decl->getAccess() == clang::AS_public) return true;
    m_rewriter.ReplaceText(access_decl->getSourceRange(), "public:");
    return true;
}

// `class D : B` and `struct S : private B` become `class D : public B` and `struct S : public B`:
// a copy of a member, `f__mono...(D *__this)`, converts its object to its bases outside the class.
void AccessSpecifierVisitor::makePublic(const clang::CXXBaseSpecifier& base, bool in_class) {
    switch (base.getAccessSpecifierAsWritten()) {
    case clang::AS_public:
        return;
    case clang::AS_none:
        // a struct's bases are public already
        if (in_class) m_rewriter.InsertTextBefore(base.getBeginLoc(), "public ");
        return;
    default: {
        // the access of `virtual private B` follows `virtual`
        const clang::SourceManager& sm = m_ctx.getSourceManager();
        clang::SourceLocation access = base.getBeginLoc();
        if (base.isVirtual() &&
            clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(access), sm, m_ctx.getLangOpts()) == "virtual")
            access = clang::Lexer::findNextToken(access, sm, m_ctx.getLangOpts())->getLocation();
        m_rewriter.ReplaceText(clang::CharSourceRange::getTokenRange(access), "public");
    }
    }
}

// `class C { int x; };` => `class C { public: int x; };`
// `class C : {access-specifier} B {...}` => `class C : public B {...}`
bool AccessSpecifierVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl* record) {
    if (!record->isThisDeclarationADefinition() || record->isLambda()) return true;
    // An explicit instantiation, `template class X<int>;` or a kept `extern template class X<int>;`,
    // is visited (implicit instantiations aren't) and is a definition once instantiated, but its
    // braces are the template's text, which the template's own visit makes public.
    // Rewriting them again would write `public public B`. An explicit or partial specialization,
    // `template <> class X<int> { ... };`, has text of its own and is rewritten.
    if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
        spec && spec->getSpecializationKind() != clang::TSK_ExplicitSpecialization)
        return true;
    // in the fallback the bases keep their access too, which traits can tell, eg.
    // `std::is_convertible_v<D*, B*>` is false for a private base
    if (!m_keep_observable_access)
        for (const clang::CXXBaseSpecifier& base : record->bases()) makePublic(base, record->isClass());
    // the members before the first access specifier of a class are private
    if (clang::SourceLocation brace = record->getBraceRange().getBegin(); brace.isValid())
        m_rewriter.InsertTextAfterToken(brace, "\npublic:");
    return true;
}

}  // namespace invisible
