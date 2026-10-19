#pragma once

// The helpers every pass shares: running clang on a string, checking that what a pass wrote
// compiles, the names functions are left as written by, and the small AST and text utilities
// that let a pass rewrite an expression or print code elsewhere, eg. at the end of the TU.

#include <set>

#include "clang/AST/ASTContext.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Sema/Sema.h"
#include <functional>
#include <string>

namespace invisible {

/// Settings shared by every pass, set once by the command line driver, see main.cpp. A pass is
/// run from deep inside clang's callbacks, which have no room for options, so they are global.
struct ToolSettings {
    bool verbose = false;  ///< show the passes' progress chatter
    bool color = false;    ///< color clang's diagnostics
};
ToolSettings& settings();

/// Stream for the passes' progress chatter, only shown with --verbose, eg. decl-before-def's
/// `decl-before-def: attempt 2 left 3 as written and 1 copies out`. Otherwise it discards.
llvm::raw_ostream& verbose();

/// Adds the flags every pipeline parses with to the user's clang flags: `-fno-builtin`, so that a
/// hoisted temporary is typed `size_t` and not clang's `__size_t`, and `-Wno-everything`, which
/// checkAsCompiled takes back when the input compiles without it.
void addPipelineFlags(std::vector<std::string>& args);

/// Makes @p args, given addPipelineFlags, check the output of the passes as strictly as a
/// compiler building it: the warnings clang makes errors by default stay errors, eg. a
/// `constexpr` specialization that can never be a constant expression before C++23. Only an
/// input, @p source, that compiles so is held to it; one that needs every warning silenced, eg.
/// C passing incompatible pointers, keeps them silenced.
void checkAsCompiled(std::vector<std::string>& args, const std::string& source, const std::string& filename);

/// The last source isCompilable() rejected, and what clang reported for it. The driver reads the
/// errors back to blame them on functions, eg. `a.cpp:812:5: error: ...` on the function that
/// line 812 is in, see functionsAround in invisible.cpp.
struct CompileFailure {
    std::string source;
    std::string filename;     ///< name the diagnostics refer to
    std::string diagnostics;  ///< clang's rendered diagnostics
    unsigned errors = 0;
};
const CompileFailure& lastCompileFailure();

/// Callback invoked once the AST of an in-memory source file is ready, with the Sema that built
/// it, which some passes ask to instantiate or look up what the source doesn't, eg. a move
/// constructor that a hoisted temporary will call.
using TUCallback = std::function<void(clang::ASTContext&, clang::Sema&)>;

/// The source as @p rewriter left it, or @p original when it changed nothing, as it then has no
/// buffer for the file.
std::string getRewrittenSource(clang::Rewriter& rewriter, const std::string& original);

/// Runs @p callback on the AST of @p source, parsed in memory with @p args as if it were the file
/// @p filename, and returns the source as @p rewriter left it. Every pass is one such run:
///
///     rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
///     MyVisitor(ctx, rewriter).TraverseDecl(ctx.getTranslationUnitDecl());
///
/// Clang's diagnostics go to verbose(), since the caller checks the result with isCompilable().
std::string runInMemory(const std::string& source, const std::string& filename, std::vector<std::string>& args, TUCallback callback,
                        clang::Rewriter& rewriter);

/// Whether @p source compiles, as `clang -fsyntax-only` with @p compiler_flags would tell. The
/// driver checks every iteration of every pass with it, so that a pass that writes code that
/// doesn't compile is caught at once, eg. hoist-calls naming a type that isn't visible where
/// the temporary is declared. Diagnostics aren't printed; when the source doesn't compile, it is
/// recorded with them in lastCompileFailure().
bool isCompilable(const std::string& source, const std::string& filename, const std::vector<std::string>& compiler_flags);

/// Whether @p decl is written inside a function or block, eg. a local class `struct Node` in
/// `void f() { struct Node {...}; }`, which nothing outside the function can name.
bool isInsideFunction(const clang::Decl* decl);

/// Whether @p type can be named anywhere in the TU: it isn't local to a function, unnamed or
/// the closure type of a lambda, nor does it name one, eg. as a template argument. Code printed
/// at the end of the TU, a copy or a hoisted temporary can only spell such a type, eg. not
/// `std::vector<(lambda at a.cc:3:9)>` or `View<__invisible_lambda_2>` of a local struct.
bool isNameableAnywhere(clang::QualType type);

/// Whether @p stmt is or contains a call, eg. `i < v.size()` does, `i < n` doesn't. The passes
/// that make room for hoisting, eg. loop-to-ir and short-circuit-to-ir, only rewrite what does.
bool containsCall(const clang::Stmt* stmt);

/// The characters of the expression @p range. An expression ending a template argument list
/// ends with a single `>`, also when the lexer saw it as part of a `>>` closing two lists, eg.
/// `idx<int>` in `in_place<idx<int>>`, of which a token range would take both, and replacing it
/// would eat the `>` of `in_place<...>`.
clang::CharSourceRange exprCharRange(clang::SourceRange range, const clang::SourceManager& sm, const clang::LangOptions& lang_opts);

/// Replaces the text of the expression @p range with @p text, apart from the words around it:
/// the text of an expression can touch one, eg. `(res)` in `not(res)` or `(a)` in `(a)or b`.
void replaceExpr(clang::Rewriter& rewriter, clang::SourceRange range, const std::string& text);

/// The printing policy of the code the passes write. A template argument is printed with its
/// type, eg. `VM<int, (Mode)5>`: a value of an enumeration that is none of its enumerators, eg. a
/// combination of flags, isn't converted to the enumeration implicitly, and `VM<int, 5>` wouldn't
/// compile. An integer gets the suffix of its type, eg. `array<int, 3UL>`.
clang::PrintingPolicy codePolicy(const clang::ASTContext& ctx);

/// The suffix an integer literal of @p value needs to be of @p type, for constant-fold: an
/// unsuffixed one is of the first of int, long and long long its magnitude fits
/// in. eg. `std::min(__diffmax, n)` with a size_t `__diffmax` folds to
/// `std::min(2305843009213693951UL, n)`, which still deduces `size_t`.
std::string integerSuffix(const clang::ASTContext& ctx, const llvm::APSInt& value, clang::QualType type);

/// Whether INVISIBLE_MONO is set to something other than empty or `0`, which turns on
/// monomorphization, see DeclBeforeDefVisitor and docs/DESIGN.md section 7.
bool monoMode();

/// The name of the monomorphized copy of the function of mangled name @p key, named @p base, eg.
/// `size__mono1a2b3c4d5e`: the same in every TU, so that the copies merge at link time.
std::string copyName(llvm::StringRef base, llvm::StringRef key);
/// Whether @p func is a monomorphized copy, by its name, see copyName.
bool isCopy(const clang::FunctionDecl* func);

/// The functions, by passKey, the pass being run leaves as written: what it made of them didn't
/// compile, see the per-function fallback, docs/DESIGN.md section 4.3. eg. when hoist-calls
/// writes `auto __func_hoist_ret__3 = ...` that doesn't compile in `f`, the pass runs again with
/// `f`'s key here and leaves `f` alone. The driver clears it before each pass.
std::set<std::string>& passSkips();
/// Whether @p func is one of passSkips.
bool isSkippedByPass(const clang::FunctionDecl* func);
/// The name a function is left as written by: its mangled name, eg. `_ZN3Vec4sizeEv`, or for a
/// template, which can't be mangled, its qualified name and number of parameters, eg.
/// `template std::vector::push_back/1`. It is stable across the runs of a pass, which each parse
/// the source again and so have other Decl pointers.
std::string passKey(const clang::FunctionDecl* func);

/// Whether the passes after DeclBeforeDef leave @p decl as written: a template or anything in one,
/// whose text all its instantiations share, a friend, which stays exactly as written, what the
/// pass's fallback skips, and in monomorphization a constexpr function left in place, which the
/// compiler may evaluate, eg. `constexpr int sq(int x) { return x * x; }` stays `return x * x;`.
bool isLeftAsWritten(const clang::Decl* decl);

/// The visitor of a pass after DeclBeforeDef: it doesn't traverse what isLeftAsWritten, nor the
/// body of a generic lambda, a template that a lambda expression, eg. of a global's initializer,
/// would otherwise traverse, eg. `auto print = [](auto x) { log(fmt(x)); };`, whose `fmt(x)` is
/// dependent and can't be hoisted. A pass derives from it instead of RecursiveASTVisitor:
///
///     class LoopToIRVisitor : public LaterPassVisitor<LoopToIRVisitor> { ... };
template <class Derived> class LaterPassVisitor : public clang::RecursiveASTVisitor<Derived> {
    using Base = clang::RecursiveASTVisitor<Derived>;

  public:
    bool TraverseDecl(clang::Decl* decl) { return isLeftAsWritten(decl) || Base::TraverseDecl(decl); }
    bool TraverseLambdaExpr(clang::LambdaExpr* lambda, typename Base::DataRecursionQueue* queue = nullptr) {
        return lambda->getCallOperator()->isDependentContext() || Base::TraverseLambdaExpr(lambda, queue);
    }
};

/// Whether @p func is a copy clang made while instantiating, whose text is the template's: an
/// instantiation, eg. `vector<int>::size`, but also a declaration of an instantiated class that
/// isn't marked as one, eg. a member specialized later, a member template or a friend. A pass
/// mustn't rewrite the template's text through it.
bool isInstantiatedCopy(const clang::FunctionDecl* func);

/// Respells `__va_list_tag` in @p text as `__builtin_va_list`. Where a va_list is an array, eg.
/// `__va_list_tag[1]` on x86-64, a va_list parameter is printed `__va_list_tag *ap`, decayed, or
/// `__va_list_tag ap[1]` when printed canonically, but `__va_list_tag` is a name of clang's that
/// can't be written in the source. `__builtin_va_list ap` decays to the same parameter type.
void respellDecayedVaList(std::string& text);

/// The scope of @p dc from the global namespace, without the unnamed and inline
/// namespaces and the transparent contexts, eg. an unscoped enum: `::std::` for a member of
/// `std::__cxx11`, an inline namespace.
clang::NestedNameSpecifier buildQualifierForContext(clang::ASTContext& ctx, clang::DeclContext* dc);

/// Names the functions, variables and enumerators in @p node that aren't local to
/// a function from the global namespace, so that @p node can be printed elsewhere, eg. the body
/// of a copy printed at the end of the TU outside every namespace: `return npos;` in a member of
/// `std::string` becomes `return ::std::basic_string<char>::npos;`.
void qualifyNonLocalNames(clang::Stmt*& node, clang::ASTContext& ctx);

/// Replaces the `this` of @p node, but not of the records in it, with @p replacement, eg. with
/// `__this` in the copy of a member function, a free function taking its object first:
/// `return this->n;` becomes `return __this->n;`.
void replaceThis(clang::Stmt* node, clang::Expr* replacement);

/// Whether the prvalue @p expr initializes an object, eg. a parameter taken by value or the
/// returned value, rather than being bound to a reference or having a member accessed,
/// which materializes it as a temporary. hoist-calls asks it to know whether the temporary it
/// writes must be moved into place, eg. `take(make())` becoming
/// `T tmp = make(); take(static_cast<T &&>(tmp));`, and decl-before-def to have the move
/// constructor that will do it instantiated, see instantiateConstructorsOfValues.
bool isUsedByValue(clang::ASTContext& ctx, const clang::Expr* expr);

/// The statement @p call is all of already, directly in a block: the DeclStmt of `int x = f();`, the
/// assignment of `x = f();` or `a = f();`, or the call of `f();` returning void; null otherwise.
/// hoist-calls leaves such a call as it is, and the inliner pastes inlined code before the statement.
const clang::Stmt* topLevelStatement(clang::ASTContext& ctx, const clang::CallExpr* call);

}  // namespace invisible
