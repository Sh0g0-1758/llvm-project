#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace invisible {

// --- KeepInPlaceVisitor ------------------------

// Finds the functions DeclBeforeDef must leave where they are written, with their definitions.
// Their instantiations stay too. These are the functions whose definition is needed there by
// code that isn't moved:
//  - a function used outside any function body, and what that function uses, eg. `small` in
//
//      template <class T> constexpr bool small() { return sizeof(T) < 4; }
//      template <class T, class = std::enable_if_t<small<T>()>> void f(T);
//
//    The declaration of `f` calls `small` where it is written, so `small` must be defined
//    before it;
//  - what a constant expression calls, eg. in a template argument or a static_assert, even
//    inside a body. The compiler evaluates it where it is written;
//  - an explicitly instantiated template;
//  - a function whose type or class can't be named elsewhere, eg. one returning a lambda's
//    closure type;
//  - a constexpr function that isn't a template. A constant expression may call it through a
//    concept or argument dependent lookup, and these calls can't all be found;
//  - the functions in @p names (by passKey), which the driver found must stay as written.
// Of the functions used outside the bodies, and what they use, only templates and constexpr
// functions are kept. Any other function only needs a declaration before the use, and the
// declaration DeclBeforeDef writes in place of its definition is enough, eg. `base` in
// `template <class T> T add(T x, T y = T(base()))`. A function with a deduced return type used
// outside the bodies is kept too: its type is only known from its body.
//
// Monomorphization reduces every other template to a declaration. So when an instantiation can
// be neither copied nor specialized, its template is kept. An instantiation used where a call
// can't be redirected to a copy, eg. through its address or from the body clang generates for
// an implicit member, is specialized instead of copied.
//
// How it works: it records the offset in the main file of every use of a function (a name, a
// member, a constructor or a destructor) and the range of every function body. It keeps what is
// used outside the bodies. Then it keeps what the text of each kept function uses, and repeats
// until nothing new is kept. It runs on the same parse as DeclBeforeDef, before DeclBeforeDef
// changes the AST.
class KeepInPlaceVisitor : public clang::RecursiveASTVisitor<KeepInPlaceVisitor> {
  public:
    // @p names: the passKeys of the functions the driver found must stay as written, see
    // laterUnmoved in invisible.cpp. @p dropped: the mangled names of the functions whose copy
    // didn't compile. A used instantiation among them keeps its template.
    explicit KeepInPlaceVisitor(clang::ASTContext& ctx, const std::set<std::string>& names = {},
                                const std::set<std::string>& dropped = {});
    // Instantiations hold the uses clang found during deduction, eg. in an enable_if. Implicit
    // members, eg. a defaulted copy constructor, call functions too.
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool shouldVisitImplicitCode() const { return true; }

    // the functions to keep, as the canonical declarations of their templates, or of the
    // functions themselves when they aren't instantiations
    std::set<const clang::FunctionDecl*> run();
    // the instantiations to write as explicit specializations instead of copies, because they are
    // used where a call can't be redirected or they can't be copied, by canonical declaration
    const std::set<const clang::FunctionDecl*>& specialized() const { return m_specialized; }

    // records a function's body and finds the seeds, eg. an explicit instantiation. Also keeps
    // what a `constexpr` variable's initializer or a static_assert calls.
    bool VisitDecl(clang::Decl* decl);
    // records the uses of functions, and keeps what a ConstantExpr calls
    bool VisitStmt(clang::Stmt* stmt);
    // sets m_in_body_assertion while a static_assert inside a body is traversed
    bool TraverseStaticAssertDecl(clang::StaticAssertDecl* assertion);

  private:
    using Range = std::pair<unsigned, unsigned>;

    // A use of `function` at `offset`. A use during template argument deduction, eg. in an
    // enable_if, is recorded where clang first needed the instantiation.
    struct Use {
        unsigned offset;
        const clang::FunctionDecl* function;
    };

    // the offset of @p loc in the main file, after macro expansion. false when it isn't in the
    // main file, eg. a builtin.
    bool offsetOf(clang::SourceLocation loc, unsigned& offset) const;
    // the offsets of the first character of @p range and of the one after its last
    bool rangeOf(clang::SourceRange range, Range& out) const;
    // the text of the declaration @p redecl, with its template head and `;`, eg. all of
    // `template <class T> T twice(T x);`. false for a declaration clang made, not one written.
    bool declRange(const clang::FunctionDecl* redecl, Range& out) const;
    // records a use of @p function at @p loc
    void addUse(clang::SourceLocation loc, const clang::FunctionDecl* function);

    // the canonical declaration of the template @p function is instantiated from, eg. `twice<T>`
    // of `twice<int>`, or of @p function itself when it isn't an instantiation
    static const clang::FunctionDecl* patternOf(const clang::FunctionDecl* function);
    // whether @p function can be declared anywhere: its type, and the class of a member, can be named
    bool isNamedAnywhere(const clang::FunctionDecl* function) const;
    // whether @p pattern has a definition written in the main file, so there is text to keep
    bool isDefinedHere(const clang::FunctionDecl* pattern) const;
    // Keeps the pattern of @p function as written, and queues its text so that run() keeps what
    // it uses. With @p templates, only a template or a constexpr function is kept: a plain
    // function only needs to be declared before its use. With @p seed, it is kept whatever it is.
    void keep(const clang::FunctionDecl* function, bool templates = false, bool seed = false);
    // keeps the functions @p stmt names or constructs with, eg. `sq` and `Pt::Pt` of
    // `static_assert(sq(Pt{2}.x) == 4)`
    void keepUsedIn(const clang::Stmt* stmt);
    // marks the instantiation @p function to be written as an explicit specialization instead of
    // a copy. When it can't be specialized either, its template is kept.
    void specialize(const clang::FunctionDecl* function);
    // specializes the functions @p stmt names, eg. those a defaulted copy constructor calls
    void specializeUsedIn(const clang::Stmt* stmt);

    clang::ASTContext& m_ctx;
    // the text of the main file, to find the `;` after a declaration
    llvm::StringRef m_text;
    const std::set<std::string>& m_names;
    const std::set<std::string>& m_dropped;
    // whether the TU is C++: C has no templates to copy
    bool m_mono;
    // whether the traversal is inside a static_assert inside a body
    bool m_in_body_assertion = false;
    // the callee expressions of the calls seen. A reference to a function that isn't one of them
    // takes the function's address.
    std::set<const clang::Expr*> m_callees;
    std::set<const clang::FunctionDecl*> m_specialized;

    // the ranges of the bodies of definitions, with the member initializers of constructors
    std::vector<Range> m_bodies;
    std::vector<Use> m_uses;
    // what run() returns
    std::set<const clang::FunctionDecl*> m_kept;
    // the ranges of kept text whose uses run() still has to keep, and the `templates` argument
    // of keep() for those uses
    std::vector<std::pair<Range, bool>> m_work;
};

}  // namespace invisible
