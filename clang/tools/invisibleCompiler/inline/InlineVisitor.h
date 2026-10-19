#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Sema/Sema.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/FoldingSet.h"
#include "clang/AST/ASTTypeTraits.h"

#include <map>
#include <set>
#include <unordered_map>
#include <deque>
#include <string>
#include <vector>
#include <cassert>

namespace invisible {

/// One line of the oracle: LLVM inlined @p callee into @p caller at @p line of the preprocessed
/// file, both named by their mangled names, as `-O2`'s optimization records report them, eg.
///
///     _Z5totalR3VecIiE _Z6squarei 28
///
/// says that the call of `square(int)` on line 28, in `total(Vec<int>&)`, was inlined.
struct InlineEntry {
    std::string caller;
    std::string callee;
    size_t line;
};

// ─── FunctionParamRenamer ───────────────────────────────────────────

/// Renames the parameters of a callee in its AST to the variables the inlined body binds them
/// to, eg. `n` to `__inline_param__3_0`, by renaming the ParmVarDecl every DeclRefExpr to one
/// refers to. A PrinterHelper isn't asked to print the initializer of a declaration, nor the
/// size expression of a variably modified type, eg. `int a[n]`, so those names are changed in
/// the AST itself, which is only printed afterwards. @p PR maps a parameter's index to its name.
struct FunctionParamRenamer : clang::RecursiveASTVisitor<FunctionParamRenamer> {
public:
    FunctionParamRenamer(clang::ASTContext &C, std::map<int, std::string> &PR);
    bool VisitDeclRefExpr(clang::DeclRefExpr *DRE);
private:
    clang::ASTContext &Ctx;
    std::map<int, std::string> &callerParams;
};

// ─── InlinePrinterHelper ───────────────────────────────────────────

/// Prints the body of a callee as the code pasted in place of a call:
///  - a parameter is printed as the variable it is bound to, see FunctionParamRenamer;
///  - `return e;` becomes `{ __inline_ret__3 = e; goto __inline_exit_3; }`, an assignment of
///    the result variable declared before the body and a jump to the label after it, or for a
///    returned reference `{ T &__inline_ref__ = e; __inline_ret__3 = __builtin_addressof(__inline_ref__); goto ...; }`;
///  - `__func__` is printed as the callee's name, which it names, not the caller's;
///  - `asm` is printed `__asm__`, a keyword in every language mode.
/// eg. inlining `int square(int x) { return x * x; }` at `int y = square(a);` prints
///
///     int __inline_ret__3;
///     {int __inline_param__3_0 = a;
///     { __inline_ret__3 = __inline_param__3_0 * __inline_param__3_0; goto __inline_exit_3; }
///     __inline_exit_3: ;}
///     int y = __inline_ret__3;
class InlinePrinterHelper : public clang::PrinterHelper {
  public:
    InlinePrinterHelper(clang::ASTContext& ctx, std::string target_var, std::string bind_type,
                        std::map<int, std::string> caller_params, clang::PrintingPolicy policy,
                        size_t current_index);

    bool handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) override;

  private:
    clang::ASTContext& m_ctx;
    std::string m_target_var;
    std::string m_bind_type;
    std::map<int, std::string> m_caller_params;
    clang::PrintingPolicy m_policy;
    size_t m_current_index;
    std::map<std::string, std::string> m_goto_labels;
    /// renames the parameters named in @p T, eg. `n` in `int[n]` or `typeof(n)`, see FunctionParamRenamer
    void renameFuncParamInType(clang::QualType T);
    FunctionParamRenamer m_func_param_renamer;
    size_t m_goto_label_index;
};

// ─── InlineVisitor ─────────────────────────────────────────────────

/// The calls of a TU by line and the mangled names of its constructors and destructors. A round
/// inlines many sites from one parse, and each site is looked up by its line, so the TU is walked
/// once to build this rather than once per site, see buildCallIndex.
struct CallIndex {
    std::map<unsigned, std::vector<clang::CallExpr*>> calls;
    std::set<std::string> structors;
};
CallIndex buildCallIndex(clang::ASTContext& ctx);

/// Inlines one oracle entry: finds the call of the callee on the entry's line in the caller and
/// replaces it with the callee's body, see InlinePrinterHelper. The result is kept in a variable
/// declared before the statement the call is in, which the call is replaced with:
///
///     int &r = at__mono1492d308f1(v, i);      // oracle: caller at__mono1492d308f1 27
///     becomes
///     int *__inline_ret__0;
///     {Vec<int> *__inline_param__0_0 = v;
///      int __inline_param__0_1 = i;
///      { int &__inline_ref__ = __inline_param__0_0->data[__inline_param__0_1];
///        __inline_ret__0 = __builtin_addressof(__inline_ref__); goto __inline_exit_0; }
///     __inline_exit_0: ;}
///     int &r = static_cast<int &>(*__inline_ret__0);
///
/// A site found but not inlined is told by skipped(), eg. a constructor, or a call that isn't a
/// statement of its own.
class InlineVisitor : public clang::RecursiveASTVisitor<InlineVisitor> {
  public:
    InlineVisitor(clang::ASTContext& ctx, const std::string& caller, const std::string& callee,
                  std::vector<size_t>& call_sites, size_t& current_index,
                  clang::Rewriter& rewriter, std::map<std::string, clang::FunctionDecl*>& decl_map,
                  std::map<const clang::Stmt*, clang::VarDecl*>& this_vars);
    /// Looks the site up in @p index, and inlines it. A constructor or destructor is matched by its
    /// mangled name alone, and skipped.
    void run(const CallIndex& index);
    /// Inlines @p call of @p callee, a call of the oracle's callee on the entry's line in the caller,
    /// see run, when it is the entry's site, or tells why it isn't inlined (skipped). The checks,
    /// in order, each with an example it rejects:
    ///  - a variadic callee, `log(const char *, ...)`, whose `va_arg`s need a real call;
    ///  - the call must be a statement of its own (topLevelStatement), `int x = f();`, `x = f();` or
    ///    `f();`, which hoist-calls makes every call it can, and not initialize a static, thread
    ///    local or constexpr variable;
    ///  - a callee with a static local, `int next() { static int n; return ++n; }`, which each
    ///    pasted copy would duplicate;
    ///  - a result of a class that isn't trivially default constructible and copyable, eg.
    ///    `std::string name()`, which the variable before the body couldn't be declared and then
    ///    assigned.
    /// Temporaries of the call that the inlined code may outlive aren't checked, see the definition.
    bool VisitCallExpr(clang::CallExpr* call, clang::FunctionDecl* callee);
    bool shouldVisitImplicitCode() const;

    /// Whether this visitor found the call site.
    bool matched() const;
    /// Why the call site found isn't inlined, empty if it is.
    const std::string& skipped() const;
    /// The lines the edit added less those it replaced: the oracle lines below it move by as many.
    size_t get_net_change();
    /// The line the inlined code was put at and how many lines it spans, which tell the compile
    /// errors in it, see inline_functions
    size_t m_inserted_line = 0;
    size_t m_inserted_span = 0;

  private:
    clang::ASTContext& m_ctx;
    std::string m_caller;
    std::string m_callee;
    std::vector<size_t>& m_call_sites;
    size_t& m_current_index;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    clang::ASTNameGenerator m_names;
    /// whether @p func is the caller, by its mangled name, a constructor or destructor by any of its variants
    bool isCaller(const clang::FunctionDecl* func);
    /// whether @p stmt is in the caller: the innermost function around it, eg. not a member of a local class
    bool inCaller(const clang::Stmt* stmt);
    bool m_matched = false;
    std::string m_skipped;
    size_t m_net_lines = 0;
    /// the definitions by mangled name, for a call through a function pointer, whose callee
    /// the AST doesn't know, see FunctionDeclMapVisitor
    std::map<std::string, clang::FunctionDecl*>& m_function_decl_map;
    /// the variable `this` was replaced with in a callee's body, by the body: a body inlined at
    /// several sites of a round is rewritten once, and its variable renamed for each site
    std::map<const clang::Stmt*, clang::VarDecl*>& m_this_vars;
};

// ─── FunctionDeclMapVisitor ─────────────────────────────────────────────────

/// Collects the function definitions of a TU by mangled name. A call through a function
/// pointer, eg. `fp(x)` where LLVM saw `fp` is `square`, has no direct callee in the AST, and
/// the oracle's callee name is looked up here instead.
class FunctionDeclMapVisitor : public clang::RecursiveASTVisitor<FunctionDeclMapVisitor> {
  public:
    FunctionDeclMapVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    std::map<std::string, clang::FunctionDecl*>& get_function_decl_map();

  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    clang::ASTNameGenerator m_names;
    std::map<std::string, clang::FunctionDecl*> m_function_decl_map;
};

/// The inlined source, and for each oracle entry whether it was inlined.
struct InlineResult {
    std::string source;
    std::vector<bool> succeeded;
};

/// Inlines the sites of @p entries into their callers in @p source, in rounds checked by a
/// compile each, see the comment at its definition. A site that can't be found, is skipped, or
/// whose inlined code doesn't compile, is reported on stderr and left as a call.
InlineResult inline_functions(const std::string& source, const std::string& filename,
                              const std::vector<InlineEntry>& entries, std::vector<std::string>& args);



}  // namespace invisible
