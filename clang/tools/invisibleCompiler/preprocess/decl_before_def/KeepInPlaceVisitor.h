#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace invisible {

// ─── KeepInPlaceVisitor ────────────────────────

/// Finds the functions DeclBeforeDef leaves as written with their specializations, like friends,
/// as what stays in place needs their definitions where it is written:
///  - what is used outside function bodies, and what that uses, eg. `small` in
///
///      template <class T> constexpr bool small() { return sizeof(T) < 4; }
///      template <class T, class = std::enable_if_t<small<T>()>> void f(T);
///
///    which instantiates what it uses before a specialization could be declared;
///  - what a constant expression calls, eg. in a template argument or a static_assert, also in
///    a body, which the compiler evaluates where it is written;
///  - an explicitly instantiated template, a function whose type or class can't be named
///    elsewhere, eg. of a return type deduced to name a lambda's closure type, and in
///    monomorphization a constexpr function that isn't a template, which a constant expression
///    may evaluate through a concept or argument dependent lookup;
///  - the functions of @p names (passKey), which a later pass needs as written.
/// Of what these use, only the templates and constexpr functions are kept, the others being
/// declared before they are called.
///
/// Monomorphization reduces the other templates to declarations, so it also keeps what an
/// instantiation that can be neither copied nor specialized uses. An instantiation named where a
/// call can't be redirected, eg. by an implicit member or by its address, is specialized instead.
///
/// It works on offsets in the main file: it collects every use of a function (where a name, a
/// member, a constructor or a destructor is used) and every function body, then keeps what is used
/// outside the bodies and, from the ranges of what it keeps, what those ranges use, until nothing
/// new is kept (a closure). It runs before DeclBeforeDef changes the AST, on the same parse.
class KeepInPlaceVisitor : public clang::RecursiveASTVisitor<KeepInPlaceVisitor> {
  public:
    /// @p names are the passKeys the driver learned must stay as written, see laterUnmoved in
    /// invisible.cpp; @p dropped the functions that get no copy, whose templates must then stay.
    explicit KeepInPlaceVisitor(clang::ASTContext& ctx, const std::set<std::string>& names = {},
                                const std::set<std::string>& dropped = {});
    /// an instantiation is where uses during deduction, eg. in an enable_if, are found, and an
    /// implicit member, eg. a defaulted copy constructor, calls what it calls by name
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool shouldVisitImplicitCode() const { return true; }

    /// the canonical declarations of the patterns of the functions found
    std::set<const clang::FunctionDecl*> run();
    /// the instantiations monomorphization specializes rather than keeps the template of, as they
    /// are named where a call can't be redirected or can't be copied, by their canonical declarations
    const std::set<const clang::FunctionDecl*>& specialized() const { return m_specialized; }

    /// collects a function's body and seeds, eg. an explicit instantiation; and keeps what a
    /// `constexpr` variable's initializer or a static_assert calls
    bool VisitDecl(clang::Decl* decl);
    /// collects the uses of functions; keeps what a ConstantExpr calls
    bool VisitStmt(clang::Stmt* stmt);
    /// tells VisitDecl and VisitStmt whether they are in a static_assert in a body, see m_in_body_assertion
    bool TraverseStaticAssertDecl(clang::StaticAssertDecl* assertion);

  private:
    using Range = std::pair<unsigned, unsigned>;

    /// A use of `function` at `offset`. A use during template argument deduction,
    /// eg. in an enable_if, is at the point where the specialization was first needed.
    struct Use {
        unsigned offset;
        const clang::FunctionDecl* function;
    };

    /// the offset of @p loc in the main file, through macro expansions; false when it isn't in it,
    /// eg. a builtin
    bool offsetOf(clang::SourceLocation loc, unsigned& offset) const;
    /// the offsets of the first and past the last character of @p range
    bool rangeOf(clang::SourceRange range, Range& out) const;
    /// the text of the declaration @p redecl, its template head and `;` included, eg. all of
    /// `template <class T> T twice(T x);`; false for a declaration clang made rather than one written
    bool declRange(const clang::FunctionDecl* redecl, Range& out) const;
    /// records a use of @p function at @p loc
    void addUse(clang::SourceLocation loc, const clang::FunctionDecl* function);

    /// the template an instantiation is of, eg. `twice<T>` of `twice<int>`, by its canonical declaration
    static const clang::FunctionDecl* patternOf(const clang::FunctionDecl* function);
    /// whether @p function can be declared anywhere: its type, and the class of a member, can be named
    bool isNamedAnywhere(const clang::FunctionDecl* function) const;
    /// whether @p pattern has a definition written in the main file, which keeping it keeps
    bool isDefinedHere(const clang::FunctionDecl* pattern) const;
    /// Keeps the pattern of @p function as written, and queues its text for the uses in it. With
    /// @p templates, only a template or a constexpr function is kept, a plain function being
    /// declared before any call, unless @p seed, which is kept whatever it is.
    void keep(const clang::FunctionDecl* function, bool templates = false, bool seed = false);
    /// keeps the functions @p stmt names or constructs with, eg. `sq` and `Pt::Pt` of
    /// `static_assert(sq(Pt{2}.x) == 4)`
    void keepUsedIn(const clang::Stmt* stmt);
    /// has monomorphization specialize the instantiation @p function rather than copy it, or keep
    /// its template when it can't be specialized either
    void specialize(const clang::FunctionDecl* function);
    /// specializes the functions @p stmt names, eg. those a defaulted copy constructor calls
    void specializeUsedIn(const clang::Stmt* stmt);

    clang::ASTContext& m_ctx;
    /// the text of the main file, for the `;` after a declaration
    llvm::StringRef m_text;
    const std::set<std::string>& m_names;
    const std::set<std::string>& m_dropped;
    bool m_mono;
    /// whether what is traversed is the assertion of a static_assert in a body
    bool m_in_body_assertion = false;
    /// the callees of the calls seen, so that a name that isn't one is told to be an address
    std::set<const clang::Expr*> m_callees;
    std::set<const clang::FunctionDecl*> m_specialized;

    /// the bodies and member initializers of definitions
    std::vector<Range> m_bodies;
    std::vector<Use> m_uses;
    /// what run() returns
    std::set<const clang::FunctionDecl*> m_kept;
    /// the ranges whose uses are kept, and whether only templates are
    std::vector<std::pair<Range, bool>> m_work;
};

}  // namespace invisible
