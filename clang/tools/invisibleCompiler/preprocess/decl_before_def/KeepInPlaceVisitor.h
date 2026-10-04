#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include <set>
#include <utility>
#include <vector>

namespace invisible {

// ─── KeepInPlaceVisitor ────────────────────────

/// Finds the functions used outside function bodies, and what they use, which
/// DeclBeforeDef leaves as written with their specializations, like friends.
///
/// DeclBeforeDef defers and specializes functions, but what stays in place, eg. a
/// constant expression, may need the definition of what it uses where it is
/// written, and it instantiates what it uses before the specialization can be
/// declared. eg. `small` in
///
/// template <class T> constexpr bool small() { return sizeof(T) < 4; }
/// template <class T, class = std::enable_if_t<small<T>()>> void f(T);
class KeepInPlaceVisitor : public clang::RecursiveASTVisitor<KeepInPlaceVisitor> {
  public:
    explicit KeepInPlaceVisitor(clang::ASTContext& ctx);
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool shouldVisitImplicitCode() const { return true; }

    /// the canonical declarations of the patterns of the functions found
    std::set<const clang::FunctionDecl*> run();

    bool VisitDecl(clang::Decl* decl);
    bool VisitStmt(clang::Stmt* stmt);

  private:
    using Range = std::pair<unsigned, unsigned>;

    /// A use of `function` at `offset`. A use during template argument deduction,
    /// eg. in an enable_if, is at the point where the specialization was first needed.
    struct Use {
        unsigned offset;
        const clang::FunctionDecl* function;
    };

    bool offsetOf(clang::SourceLocation loc, unsigned& offset) const;
    bool rangeOf(clang::SourceRange range, Range& out) const;
    bool declRange(const clang::FunctionDecl* redecl, Range& out) const;
    void addUse(clang::SourceLocation loc, const clang::FunctionDecl* function);

    static const clang::FunctionDecl* patternOf(const clang::FunctionDecl* function);
    bool isDefinedHere(const clang::FunctionDecl* pattern) const;
    void keep(const clang::FunctionDecl* function);

    clang::ASTContext& m_ctx;
    llvm::StringRef m_text;

    /// the bodies and member initializers of definitions
    std::vector<Range> m_bodies;
    std::vector<Use> m_uses;
    std::set<const clang::FunctionDecl*> m_kept;
    std::vector<Range> m_work;
};

}  // namespace invisible
