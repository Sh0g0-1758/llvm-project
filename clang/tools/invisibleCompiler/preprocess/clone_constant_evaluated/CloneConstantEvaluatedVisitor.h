#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace invisible {

// ─── CloneConstantEvaluatedVisitor ────────────────────────

/// Gives the functions constant expressions evaluate clones, and makes the
/// constant expressions call the clones.
///
/// DeclBeforeDef defers and specializes functions, but a constant expression needs
/// the definition of what it evaluates where it is evaluated, and it instantiates
/// what it uses before the specialization can be declared. The clones are left as
/// written by the passes that follow, the originals are deferred and specialized
/// like any other function.
///
/// template <class T> constexpr bool small() { return sizeof(T) < 4; }
/// template <class T, class = std::enable_if_t<small<T>()>> void f(T);
///
/// is transformed into
///
/// template <class T> constexpr bool small() { return sizeof(T) < 4; }
/// template <class T> constexpr bool __invisible_clone_small() { return sizeof(T) < 4; }
/// template <class T, class = std::enable_if_t<__invisible_clone_small<T>()>> void f(T);
///
/// The clone of an operator or a conversion is a named function and its calls are
/// spelled as calls to it, the clone of a constructor takes a leading
/// __invisible_clone_tag. An original used where a clone can't be spelled, eg. a
/// destructor, is marked to be left as written instead, with what it uses.
class CloneConstantEvaluatedVisitor : public clang::RecursiveASTVisitor<CloneConstantEvaluatedVisitor> {
  public:
    CloneConstantEvaluatedVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool shouldVisitImplicitCode() const { return true; }

    void run();

    bool TraverseDecl(clang::Decl* decl);
    bool TraverseTemplateArgumentLoc(const clang::TemplateArgumentLoc& loc);
    bool VisitDecl(clang::Decl* decl);
    bool VisitStmt(clang::Stmt* stmt);
    bool VisitTypeLoc(clang::TypeLoc loc);

  private:
    using Range = std::pair<unsigned, unsigned>;

    /// How a use is spelled. Name: by the name at `range`. Dependent: by the name at
    /// `range` in a template, which may be a function that is cloned. Operator,
    /// Conversion, Construct, Converting: by `expr`, Converting constructs from a
    /// single argument without parentheses. Point: by nothing, if not by another use at
    /// its offset. Unspelled: by nothing. Body: `range`, eg. a default member
    /// initializer, is evaluated.
    enum class Kind { Name, Dependent, Operator, Conversion, Construct, Converting, Point, Unspelled, Body };
    using Key = std::pair<unsigned, Kind>;

    struct Use {
        unsigned offset;
        Kind kind;
        const clang::FunctionDecl* function;
        const clang::Expr* expr;
        Range range;
        /// 0: not evaluated, 1: in a constant context, 2: in a function that is cloned
        char reached;
        bool instantiated;
    };

    /// Edits at the same offset are applied in increasing `order`.
    struct Edit {
        unsigned offset;
        unsigned length;
        std::string text;
        long order;
    };

    /// The edits spelling the uses at one place. Those in a function that is cloned
    /// are only made in its clone.
    struct Spelling {
        std::vector<Edit> edits;
        bool in_clone_only;
        bool conflicts;
        bool in_pattern;
    };

    bool offsetOf(clang::SourceLocation loc, unsigned& offset) const;
    bool rangeOf(clang::SourceRange range, Range& out) const;
    bool declRange(const clang::FunctionDecl* redecl, Range& out) const;
    void addRange(std::vector<Range>& ranges, const clang::Stmt* stmt);
    void addUse(Kind kind, clang::SourceLocation loc, const clang::FunctionDecl* function,
                const clang::Expr* expr = nullptr, clang::SourceRange range = {});
    void block(Kind kind, clang::SourceLocation loc);
    void visitVar(clang::VarDecl* var);
    void addDependentInit(Kind kind, clang::SourceLocation at, const clang::TypeSourceInfo* type,
                          const clang::Expr* init, clang::SourceLocation end, bool args);
    void visitUse(clang::Expr* expr);
    void visitBlocker(clang::Expr* expr);
    std::string writtenName(const Use& use) const;
    bool isSpelledByName(const std::string& name) const;

    static const clang::FunctionDecl* patternOf(const clang::FunctionDecl* function);
    bool isDefinedHere(const clang::FunctionDecl* pattern) const;
    bool isCloned(const clang::FunctionDecl* pattern) const;
    std::string cloneName(const clang::FunctionDecl* pattern) const;

    void reach(const clang::FunctionDecl* function);
    void reachAll();
    std::vector<Edit> spell(const Use& use) const;
    void keepUnspelled();
    std::string cloneOf(const clang::FunctionDecl* redecl, Range range) const;
    std::string memberInits(const clang::CXXConstructorDecl* ctor) const;
    std::vector<Edit> editsWithin(Range range) const;
    std::string apply(Range range, std::vector<Edit> edits) const;

    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    llvm::StringRef m_text;

    std::vector<Range> m_constant;
    std::vector<Range> m_unevaluated;
    std::vector<Use> m_uses;
    std::set<Key> m_blocked;
    std::set<Range> m_scalars;
    /// the callees of operator calls, and the operators rewritten comparisons are evaluated with
    std::set<const clang::Expr*> m_operator_callees;
    std::map<const clang::Expr*, std::pair<std::string, std::string>> m_rewritten;
    std::map<unsigned, const clang::Expr*> m_dependent_objects;
    /// The initializations in templates of an object of dependent type, whose
    /// arguments end at `end`: `T t = x`, `T t(x)`, `T(x)`, and `: m(x)` of a
    /// member of dependent type.
    struct DependentInit {
        std::string type;
        unsigned end;
        bool args;
    };
    std::map<Key, DependentInit> m_dependent_inits;
    std::vector<const clang::CXXMethodDecl*> m_overriders;
    unsigned m_instantiating = 0;

    std::set<const clang::FunctionDecl*> m_reached;
    std::vector<Range> m_work;
    /// What a dependent name can be, found in the TU: the functions of that name,
    /// and whether anything else, eg. a variable, has it too.
    struct Named {
        std::set<const clang::FunctionDecl*> functions;
        bool others = false;
    };
    std::map<std::string, Named> m_named;
    std::map<Key, Spelling> m_spellings;
    std::set<const clang::FunctionDecl*> m_kept;
    std::vector<Edit> m_edits;
    std::vector<Edit> m_clone_edits;
};

}  // namespace invisible
