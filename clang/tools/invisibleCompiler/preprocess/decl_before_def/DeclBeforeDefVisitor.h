#pragma once

#include "common/DeclBeforeDefPrinterHelper.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Sema/Sema.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/FoldingSet.h"
#include "clang/AST/ASTTypeTraits.h"

#include <map>
#include <unordered_map>
#include <deque>
#include <string>
#include <set>
#include <vector>
#include <cassert>

namespace invisible {

// --- DeclBeforeDefVisitor ----------------------------------------------

// decl-before-def: declares every function before any definition, and writes the definitions
// the binary has at the end of the TU. An instantiation of a template is given a copy, a free
// function of its own name, which what is deferred calls, and the template is reduced to a
// declaration; a constructor, a destructor or a virtual function is written as an explicit
// specialization instead. See docs/DESIGN.md sections 6 and 7.
//
// The inliner pastes a callee's body into its caller, so every function it inlines must have
// a body in the text, and every body must be able to call anything. eg.
//
//     template <class T> T twice(T x) { return x + x; }
//     int main() { return twice(2); }
//
// becomes
//
//     template <class T> T twice(T x);
//     int main();
//     [[gnu::used]] inline int twice__mono1a2b3c4d5e(int x);
//
//     int main(){ return twice__mono1a2b3c4d5e(2); }
//     [[gnu::used]] inline int twice__mono1a2b3c4d5e(int x){ return x + x; }
//
// The driver retries it, leaving as written what it wrote that doesn't compile, see
// entries(), replaced(), stripped() and copyKeys().
class DeclBeforeDefVisitor : public clang::RecursiveASTVisitor<DeclBeforeDefVisitor> {
  public:
    // @p kept_in_place is left as written, see KeepInPlaceVisitor
    // A template instantiation, and what is left as written, is given a copy, a free function of
    // its own name, called instead of it by what is
    // deferred, see monomorphize. @p dropped names by their mangled names the functions not given one.
    DeclBeforeDefVisitor(clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter,
                         const std::set<const clang::FunctionDecl*>& kept_in_place,
                         const std::set<std::string>& dropped = {},
                         const std::set<const clang::FunctionDecl*>& specialized = {});
    // Replaces the definition @p func where it is written by its declaration, and records it
    // for the end of the TU, see the class comment.
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    // The declarations and definitions to write at the end of the TU, after the traversal.
    std::string getDeferredDefinitions();
    // true: the instantiations of templates are what is specialized or copied.
    bool shouldVisitTemplateInstantiations() const;

    // The lines of getDeferredDefinitions printing a function, named by its mangled name, counted
    // from 1 at its first line, which is empty
    // eg. an error on line 12 of the deferred text, inside the entry of `twice<int>`, lines 10 to
    // 14, leaves `twice` as written on the next attempt, or drops the copy if it is one.
    struct Entry {
        std::string key;
        size_t first_line;
        size_t last_line;
        // whether the entry is a monomorphized copy, which is dropped rather than left as written
        bool copy;
    };
    const std::vector<Entry>& entries() const { return m_entries; }
    // the mangled names of the functions copied, by the names of their copies
    const std::map<std::string, std::string>& copyKeys() const { return m_copy_keys; }
    // the first lines of the declarations written in place of definitions, by mangled name
    const std::vector<std::pair<std::string, std::string>>& replaced() const { return m_replaced; }
    // the templates reduced to a declaration, by passKey, and the first line of the declaration
    const std::vector<std::pair<std::string, std::string>>& stripped() const { return m_stripped; }

  private:
    clang::ASTContext& m_ctx;
    clang::Sema& m_sema;
    clang::Rewriter& m_rewriter;
    // what is left as written, see KeepInPlaceVisitor
    const std::set<const clang::FunctionDecl*>& m_kept_in_place;
    // the instantiations monomorphization specializes, besides constructors, destructors and virtual functions
    const std::set<const clang::FunctionDecl*>& m_specialized;

    // -- printing policies, see the constructor --
    // the bodies of what is deferred
    clang::PrintingPolicy m_policy;
    // the member initializers of deferred constructors
    clang::PrintingPolicy m_deferred_policy_ctor;
    // the heads of the deferred declarations and definitions, canonical and fully qualified
    clang::PrintingPolicy m_deferred_policy_decl;
    // the declarations written in place of the definitions
    clang::PrintingPolicy m_inplace_policy;
    // the copies, printed from the global namespace
    clang::PrintingPolicy m_copy_policy;
    // the mangled names that identify functions to the driver
    clang::ASTNameGenerator m_names;

    // -- monomorphization --
    // the functions, by mangled name, not given a copy: theirs didn't compile on an earlier attempt
    const std::set<std::string>& m_dropped;
    // whether the TU is C++: C has no templates to copy
    bool m_mono;
    // the name of the copy of each function given one, by canonical declaration, and those functions in order
    std::map<const clang::FunctionDecl*, std::string> m_copies;
    std::vector<clang::FunctionDecl*> m_copied;
    MonoPrinterHelper m_custom_printer;

    // -- what is written at the end, see getDeferredDefinitions --
    std::vector<std::string> m_deferred_keys;
    std::vector<Entry> m_entries;
    std::map<std::string, std::string> m_copy_keys;
    std::vector<std::pair<std::string, std::string>> m_replaced;
    std::vector<std::pair<std::string, std::string>> m_stripped;

    // Chooses the functions given copies and their names, before anything is printed.
    void monomorphize();
    // Has the calls of copied functions in @p node call the copies, see DeclBeforeDefVisitor.cpp.
    void redirectCalls(clang::Stmt*& node);
    // The declaration of the function @p name that the redirected calls refer to.
    clang::FunctionDecl* printedFunction(const std::string& name);
    std::map<std::string, clang::FunctionDecl*> m_printed;
    // the callees of the calls redirectCalls went through
    std::set<const clang::Expr*> m_callees;
    // The head of the copy @p name of @p func, eg. `int get__mono...(const Box<int> *__this)`.
    std::string copyHeader(clang::FunctionDecl* func, const std::string& name);
    std::vector<std::string> m_deferred_definitions;
    std::vector<const clang::FunctionDecl*> m_deferred_functions;
    std::vector<std::string> m_deferred_declarations;

    // -- fixes to what the printer prints of a declaration, see DeclBeforeDefVisitor.cpp --
    void fixParameterPack(std::string& str, clang::FunctionDecl* func);
    void stripConstructorTemplateArgs(std::string& str, clang::CXXConstructorDecl* ctor);
    void stripDestructorExceptionSpec(std::string& str);
    unsigned countEnclosingTemplateLevels(const clang::FunctionDecl* func_decl);
    void stripRecordDeclOnlySpecifiers(std::string& s);
    std::string printCtorInitializers(clang::CXXConstructorDecl* ctor);
    // The order the deferred definitions are written in: deduced return types first.
    std::vector<size_t> deferredDefinitionOrder() const;
    void recordFunctionDefinition(clang::FunctionDecl* func, std::string& definition_head, std::string& declaration_head);
    void getFunctionDeclaration(clang::FunctionDecl* func, std::string& decl);
    void getKRFunctionDeclarations(clang::FunctionDecl* func, std::string& decl, std::string& head);
    void replaceFunctionDeclaration(clang::FunctionDecl* func, std::string& decl);
    // The ranges clang reports of some declarations miss text that must be replaced with them.
    clang::SourceLocation expandToTemplateHeaders(clang::SourceLocation begin);
    clang::SourceLocation expandToTerminatingSemi(clang::SourceLocation end);
};

// Whether the instantiation @p func is printed `constexpr`. An instantiation of a `constexpr`
// template stays `constexpr` even when it can never be a constant expression, eg. the
// constructor of `pair<const string, int>` before C++23, which clang only accepts of a template.
// Printed as a specialization, or copied, it would be an error, so it is printed without.
bool printsConstexpr(const clang::FunctionDecl* func);

// Has @p sema instantiate the constructors that moving or copying the results of class
// type used by value will call once hoist-calls names them. The results are built in place
// in the input, which can leave those constructors unused, eg. of a member of a class
// template, and without a definition once DeclBeforeDef deferred the ones used.
void instantiateConstructorsOfValues(clang::ASTContext& ctx, clang::Sema& sema);

// How many templates of the name of the specialization @p func deduce its type, given its
// template arguments or not. A specialization is declared for the one it deduces, so its
// arguments can be left out of the declaration only when exactly one does, eg.
// `template<> int abs(int)` rather than `template<> int abs<int>(int)`.
unsigned countDeducedTemplates(clang::Sema& sema, clang::FunctionDecl* func, bool with_arguments);

// Whether monomorphization can give @p func a copy: a function with a body, outside other
// functions, of types that can be named at the end of the TU, and called as any other function,
// or with @p specialized, whether it can be specialized instead. eg. `vector<int>::size()` can
// be copied, a constructor can only be specialized, and a member of a local class neither.
bool isCopyable(const clang::FunctionDecl* func, bool specialized = false);

// Whether monomorphization specializes @p func whatever calls it: a constructor, a destructor or
// a virtual function, which can't be called as a free function.
bool isAlwaysSpecialized(const clang::FunctionDecl* func);

}  // namespace invisible
