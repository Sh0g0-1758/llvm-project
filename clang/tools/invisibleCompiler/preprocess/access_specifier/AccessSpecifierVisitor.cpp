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

// With keep_observable_access, a constructor, destructor or copy or move assignment operator keeps
// the access it is declared with, eg.
//
//     protected: ~buffer() = default;   =>   public: ... protected: ~buffer() = default; public: ...
//
// Code can test that access, eg. `static_assert(!std::is_destructible_v<buffer>)` or SFINAE. The
// driver only asks for this when the output with everything public doesn't compile.
bool AccessSpecifierVisitor::VisitCXXMethodDecl(clang::CXXMethodDecl* method) {
    bool special = llvm::isa<clang::CXXConstructorDecl, clang::CXXDestructorDecl>(method) ||
                   method->isCopyAssignmentOperator() || method->isMoveAssignmentOperator();
    // an implicit or out-of-line declaration is not inside the class, so it has no label to keep
    if (!m_keep_observable_access || !special || method->isImplicit() || method->getAccess() == clang::AS_public ||
        method->isOutOfLine())
        return true;
    const clang::Decl* decl = method;
    if (const clang::FunctionTemplateDecl* tmpl = method->getDescribedFunctionTemplate()) decl = tmpl;
    const clang::SourceManager& sm = m_ctx.getSourceManager();
    clang::SourceLocation end = clang::Lexer::findLocationAfterToken(decl->getEndLoc(), clang::tok::semi, sm, m_ctx.getLangOpts(), false);
    if (end.isInvalid()) end = clang::Lexer::getLocForEndOfToken(decl->getEndLoc(), 0, sm, m_ctx.getLangOpts());
    // the old label goes before the declaration, and `public:` after it for the members that follow
    m_rewriter.InsertTextBefore(decl->getBeginLoc(), "\n" + clang::getAccessSpelling(method->getAccess()).str() + ":\n");
    m_rewriter.InsertTextAfter(end, "\npublic:\n");
    return true;
}

// `private:` and `protected:` => `public:`. Later passes use members from outside their class:
// decl-before-def moves member definitions out of it, the inliner pastes a member's body into
// other functions, and the copy of a member function is a free function.
bool AccessSpecifierVisitor::VisitAccessSpecDecl(clang::AccessSpecDecl* access_decl) {
    if (access_decl->getAccess() == clang::AS_public) return true;
    m_rewriter.ReplaceText(access_decl->getSourceRange(), "public:");
    return true;
}

// Makes @p base public, eg. `class D : B` and `struct S : private B` become `class D : public B`
// and `struct S : public B`. The copy of a member function, `f__mono...(D *__this)`, is a free
// function, and converting `__this` to a `B *` there needs a public base.
void AccessSpecifierVisitor::makePublic(const clang::CXXBaseSpecifier& base, bool in_class) {
    switch (base.getAccessSpecifierAsWritten()) {
    case clang::AS_public:
        return;
    case clang::AS_none:
        // a struct's bases are public already
        if (in_class) m_rewriter.InsertTextBefore(base.getBeginLoc(), "public ");
        return;
    default: {
        // in `virtual private B` the access keyword comes after `virtual`
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
    // is visited too. Its source locations point into the template, which is rewritten when the
    // template is visited. Rewriting it again would write `public public B`. An explicit or partial
    // specialization, `template <> class X<int> { ... };`, has its own text and is rewritten.
    if (const auto* spec = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(record);
        spec && spec->getSpecializationKind() != clang::TSK_ExplicitSpecialization)
        return true;
    // with keep_observable_access the bases keep their access too, as type traits can test it, eg.
    // `std::is_convertible_v<D*, B*>` is false for a private base
    if (!m_keep_observable_access)
        for (const clang::CXXBaseSpecifier& base : record->bases()) makePublic(base, record->isClass());
    // `public:` right after `{`, as the members before the first access specifier of a class are private
    if (clang::SourceLocation brace = record->getBraceRange().getBegin(); brace.isValid())
        m_rewriter.InsertTextAfterToken(brace, "\npublic:");
    return true;
}

}  // namespace invisible
