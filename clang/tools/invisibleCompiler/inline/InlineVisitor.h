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

// One line of the oracle: LLVM inlined `callee` into `caller` at `line` of the preprocessed file.
// Names are mangled, as in `-O2`'s optimization records. eg.
//
//     _Z5totalR3VecIiE _Z6squarei 28
//
// says that the call of `square(int)` on line 28, in `total(Vec<int>&)`, was inlined.
struct InlineEntry {
    std::string caller;
    std::string callee;
    size_t line;
};

// --- FunctionParamRenamer -------------------------------------------

// Renames a callee's parameters to their variables in the inlined code, eg. `n` =>
// `__inline_param__3_0`. It renames the ParmVarDecl itself, so every reference prints the new
// name. This also reaches what the PrinterHelper is never asked about, such as initializers and
// the size in `int a[n]`. @p PR maps a parameter's index to its new name.
struct FunctionParamRenamer : clang::RecursiveASTVisitor<FunctionParamRenamer> {
public:
    FunctionParamRenamer(clang::ASTContext &C, std::map<int, std::string> &PR);
    bool VisitDeclRefExpr(clang::DeclRefExpr *DRE);
private:
    clang::ASTContext &Ctx;
    std::map<int, std::string> &callerParams;
};

// --- InlinePrinterHelper -------------------------------------------

// Prints a callee's body as the code pasted for a call:
//  - a parameter prints as its variable, see FunctionParamRenamer;
//  - `return e;` => `{ __inline_ret__3 = e; goto __inline_exit_3; }`. It stores the result and
//    jumps to the label after the body;
//  - a returned reference => `{ T &__inline_ref__ = e;
//    __inline_ret__3 = __builtin_addressof(__inline_ref__); goto ...; }`;
//  - `__func__` => the callee's name, not the caller's;
//  - `asm` => `__asm__`, a keyword in every language mode.
// InlineVisitor::VisitCallExpr shows the whole pasted code.
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
    // renames the parameters named in @p T, eg. `n` in `int[n]` or `typeof(n)`, see FunctionParamRenamer
    void renameFuncParamInType(clang::QualType T);
    FunctionParamRenamer m_func_param_renamer;
    size_t m_goto_label_index;
};

// --- InlineVisitor -------------------------------------------------

// The calls of a TU by line, and the mangled names of its constructors and destructors. A round
// inlines many sites from one parse. This lets it walk the TU once, not once per site.
struct CallIndex {
    std::map<unsigned, std::vector<clang::CallExpr*>> calls;
    std::set<std::string> structors;
};
CallIndex buildCallIndex(clang::ASTContext& ctx);

// Inlines one oracle entry: finds the call of the callee on the entry's line in the caller and
// replaces it with the callee's body, see InlinePrinterHelper. The result is kept in a variable
// declared before the statement the call is in, which the call is replaced with:
//
//     int &r = at__mono1492d308f1(v, i);      // oracle: caller at__mono1492d308f1 27
//     becomes
//     int *__inline_ret__0;
//     {Vec<int> *__inline_param__0_0 = v;
//      int __inline_param__0_1 = i;
//      { int &__inline_ref__ = __inline_param__0_0->data[__inline_param__0_1];
//        __inline_ret__0 = __builtin_addressof(__inline_ref__); goto __inline_exit_0; }
//     __inline_exit_0: ;}
//     int &r = static_cast<int &>(*__inline_ret__0);
//
// For a site found but not inlined, skipped() says why, eg. it is a constructor.
class InlineVisitor : public clang::RecursiveASTVisitor<InlineVisitor> {
  public:
    InlineVisitor(clang::ASTContext& ctx, const std::string& caller, const std::string& callee,
                  std::vector<size_t>& call_sites, size_t& current_index,
                  clang::Rewriter& rewriter, std::map<std::string, clang::FunctionDecl*>& decl_map,
                  std::map<const clang::Stmt*, clang::VarDecl*>& this_vars);
    // Finds the site in @p index and inlines it. A constructor or destructor is skipped by its
    // mangled name alone.
    void run(const CallIndex& index);
    // Inlines @p call, or sets skipped() to say why not. run picks the call. It is skipped when:
    //  - the callee is variadic, `log(const char *, ...)`: its `va_arg`s need a real call;
    //  - the call isn't a statement of its own, `int x = f();`, `x = f();` or `f();`
    //    (topLevelStatement). hoist-calls makes most calls one;
    //  - the call initializes a static, thread local or constexpr variable;
    //  - the callee has a static local, `int next() { static int n; return ++n; }`: each pasted
    //    copy would have its own;
    //  - it returns a class that isn't trivially default constructible and copyable, eg.
    //    `std::string name()`: the result variable is declared first and assigned later.
    // Not checked: an argument that points into a temporary, eg. `__mono_addr(make())`. The
    // temporary dies once its parameter variable is initialized, before the body uses it.
    bool VisitCallExpr(clang::CallExpr* call, clang::FunctionDecl* callee);
    bool shouldVisitImplicitCode() const;

    // Whether this visitor found the call site.
    bool matched() const;
    // Why the call site found isn't inlined, empty if it is.
    const std::string& skipped() const;
    // The lines the edit added less those it replaced: the oracle lines below it move by as many.
    size_t get_net_change();
    // Where the inlined code starts and how many lines it spans. inline_functions blames a
    // compile error in these lines on this site.
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
    // whether @p func is the caller, by its mangled name, a constructor or destructor by any of its variants
    bool isCaller(const clang::FunctionDecl* func);
    // whether @p stmt is in the caller: the innermost function around it, eg. not a member of a local class
    bool inCaller(const clang::Stmt* stmt);
    bool m_matched = false;
    std::string m_skipped;
    size_t m_net_lines = 0;
    // the definitions by mangled name, for a call through a function pointer, whose callee
    // the AST doesn't know, see FunctionDeclMapVisitor
    std::map<std::string, clang::FunctionDecl*>& m_function_decl_map;
    // body => the variable that replaced `this` in it. A body is rewritten once per round, and
    // the variable is renamed for each site.
    std::map<const clang::Stmt*, clang::VarDecl*>& m_this_vars;
};

// --- FunctionDeclMapVisitor -------------------------------------------------

// Collects the function definitions of a TU by mangled name. A call through a function
// pointer, eg. `fp(x)` where LLVM saw `fp` is `square`, has no direct callee in the AST, and
// the oracle's callee name is looked up here instead.
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

// The inlined source, and for each oracle entry whether it was inlined.
struct InlineResult {
    std::string source;
    std::vector<bool> succeeded;
};

// Inlines the sites of @p entries into their callers in @p source, in rounds checked by a
// compile each, see the comment at its definition. A site that can't be found, is skipped, or
// whose inlined code doesn't compile, is reported on stderr and left as a call.
InlineResult inline_functions(const std::string& source, const std::string& filename,
                              const std::vector<InlineEntry>& entries, std::vector<std::string>& args);



}  // namespace invisible
