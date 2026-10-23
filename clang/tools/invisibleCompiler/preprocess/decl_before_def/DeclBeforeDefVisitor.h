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

// decl-before-def: moves every function definition that produces code to the end of the TU, and
// declares every function before the first of them. A template instantiation gets a copy: a
// free function with a new name, eg. `twice__mono1a2b3c4d5e`. The moved code calls the copy,
// and the template is reduced to a declaration. Constructors, destructors and virtual functions
// can't be free functions, so their instantiations are written as explicit specializations
// instead. See docs/DESIGN.md sections 6 and 7.
//
// The inliner needs this. It pastes a callee's body into its caller, so every function it
// inlines must have a body in the text, and any body must be able to call any function. eg.
//
//     template <class T> T twice(T x) { return x + x; }
//     int main() { return twice(2); }
//
// becomes
//
//     template <class T> T twice(T x);
//     int main();
//
//     template <class T> T *__mono_addr(T &&x) { return __builtin_addressof(x); }
//     int main();
//     int twice__mono1a2b3c4d5e(int x);
//
//     [[gnu::used]] inline int twice__mono1a2b3c4d5e(int x){ return x + x; }
//     int main(){ return twice__mono1a2b3c4d5e(2); }
//
// When the output doesn't compile, the driver runs the pass again and leaves the function with
// the error as written, or gives it no copy. entries(), replaced(), stripped() and copyKeys()
// let it find that function from the line of the error.
class DeclBeforeDefVisitor : public clang::RecursiveASTVisitor<DeclBeforeDefVisitor> {
  public:
    // @p kept_in_place: the functions to leave as written, see KeepInPlaceVisitor.
    // @p dropped: the mangled names of the functions whose copy didn't compile on an earlier
    // attempt. They get no copy.
    // @p specialized: the instantiations written as explicit specializations rather than copied.
    DeclBeforeDefVisitor(clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter,
                         const std::set<const clang::FunctionDecl*>& kept_in_place,
                         const std::set<std::string>& dropped = {},
                         const std::set<const clang::FunctionDecl*>& specialized = {});
    // Replaces the definition @p func where it is written by its declaration, and records it
    // for the end of the TU, see the class comment.
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    // The declarations and definitions to write at the end of the TU, after the traversal.
    std::string getDeferredDefinitions();
    // true: the instantiations are what the pass writes at the end, as copies or specializations.
    bool shouldVisitTemplateInstantiations() const;

    // Where the text of one function is in the result of getDeferredDefinitions: its mangled
    // name, and its first and last lines. Line 1 is the empty line the result starts with. The
    // driver uses it to find the function a compile error is in. eg. an error inside the entry
    // of `twice<int>` leaves `twice` as written on the next attempt, or drops the copy if the
    // entry is a copy.
    struct Entry {
        std::string key;
        size_t first_line;
        size_t last_line;
        // whether the entry is a copy. A copy with an error is dropped, not left as written.
        bool copy;
    };
    const std::vector<Entry>& entries() const { return m_entries; }
    // maps the name of each copy to the mangled name of the function it copies
    const std::map<std::string, std::string>& copyKeys() const { return m_copy_keys; }
    // for each definition replaced by a declaration: its mangled name, and the first line of
    // that declaration
    const std::vector<std::pair<std::string, std::string>>& replaced() const { return m_replaced; }
    // for each template reduced to a declaration: its passKey, and the first line of that
    // declaration
    const std::vector<std::pair<std::string, std::string>>& stripped() const { return m_stripped; }

  private:
    clang::ASTContext& m_ctx;
    clang::Sema& m_sema;
    clang::Rewriter& m_rewriter;
    // the functions left as written, see KeepInPlaceVisitor
    const std::set<const clang::FunctionDecl*>& m_kept_in_place;
    // the instantiations written as explicit specializations instead of copies. Constructors,
    // destructors and virtual functions always are, and aren't listed here.
    const std::set<const clang::FunctionDecl*>& m_specialized;

    // -- printing policies, see the constructor --
    // the bodies of the moved definitions
    clang::PrintingPolicy m_policy;
    // the member initializers of deferred constructors
    clang::PrintingPolicy m_deferred_policy_ctor;
    // the heads of the deferred declarations and definitions, canonical and fully qualified
    clang::PrintingPolicy m_deferred_policy_decl;
    // the declarations written in place of the definitions
    clang::PrintingPolicy m_inplace_policy;
    // the copies, which are written at global scope
    clang::PrintingPolicy m_copy_policy;
    // the mangled names that identify functions to the driver
    clang::ASTNameGenerator m_names;

    // -- monomorphization --
    // the mangled names of the functions not given a copy, because their copy didn't compile on
    // an earlier attempt
    const std::set<std::string>& m_dropped;
    // whether the TU is C++: C has no templates to copy
    bool m_mono;
    // m_copies maps the canonical declaration of each copied function to the name of its copy.
    // m_copied lists the same functions in the order they were found.
    std::map<const clang::FunctionDecl*, std::string> m_copies;
    std::vector<clang::FunctionDecl*> m_copied;
    // for each definition, the copies written at the end where that definition is, as indices in
    // m_copied, eg. every copy of `twice<...>` where `template <class T> T twice(T x) {...}` is
    std::map<const clang::FunctionDecl*, std::vector<size_t>> m_copies_at;
    // what is written at the end, in the order of the TU. Each pair is whether it is a copy, and
    // its index in m_copied or in m_deferred_definitions.
    std::vector<std::pair<bool, size_t>> m_written;
    MonoPrinterHelper m_custom_printer;

    // -- what is written at the end, see getDeferredDefinitions --
    std::vector<std::string> m_deferred_keys;
    std::vector<Entry> m_entries;
    std::map<std::string, std::string> m_copy_keys;
    std::vector<std::pair<std::string, std::string>> m_replaced;
    std::vector<std::pair<std::string, std::string>> m_stripped;

    // Chooses the functions given copies and their names, before anything is printed.
    void monomorphize();
    // Changes the calls in @p node of functions that have a copy into calls of the copy.
    void redirectCalls(clang::Stmt*& node);
    // A placeholder declaration named @p name for the redirected calls to refer to. It is only
    // printed.
    clang::FunctionDecl* printedFunction(const std::string& name);
    std::map<std::string, clang::FunctionDecl*> m_printed;
    // the callee expressions of the calls redirectCalls has seen. A reference to a function that
    // isn't one of them takes the function's address.
    std::set<const clang::Expr*> m_callees;
    // The head of the copy @p name of @p func, eg. `int get__mono...(const Box<int> *__this)`.
    std::string copyHeader(clang::FunctionDecl* func, const std::string& name);
    std::vector<std::string> m_deferred_definitions;
    std::vector<const clang::FunctionDecl*> m_deferred_functions;
    std::vector<std::string> m_deferred_declarations;

    // -- fixes for declarations clang's printer prints wrong, see DeclBeforeDefVisitor.cpp --
    void fixParameterPack(std::string& str, clang::FunctionDecl* func);
    void stripConstructorTemplateArgs(std::string& str, clang::CXXConstructorDecl* ctor);
    void stripDestructorExceptionSpec(std::string& str);
    unsigned countEnclosingTemplateLevels(const clang::FunctionDecl* func_decl);
    void stripRecordDeclOnlySpecifiers(std::string& s);
    std::string printCtorInitializers(clang::CXXConstructorDecl* ctor);
    // The deferred definitions with a deduced return type, as indices, each after the ones it
    // calls. They are written before the other definitions.
    std::vector<size_t> deferredDefinitionOrder() const;
    void recordFunctionDefinition(clang::FunctionDecl* func, std::string& definition_head, std::string& declaration_head);
    void getFunctionDeclaration(clang::FunctionDecl* func, std::string& decl);
    void getKRFunctionDeclarations(clang::FunctionDecl* func, std::string& decl, std::string& head);
    void replaceFunctionDeclaration(clang::FunctionDecl* func, std::string& decl);
    // clang's range of some declarations misses their template heads or their `;`. These extend it.
    clang::SourceLocation expandToTemplateHeaders(clang::SourceLocation begin);
    clang::SourceLocation expandToTerminatingSemi(clang::SourceLocation end);
};

// Whether @p func is printed `constexpr`. clang keeps `constexpr` on an instantiation of a
// `constexpr` template even when the instantiation can never be a constant expression, eg. the
// constructor of `pair<const string, int>` before C++23. That is only allowed for an
// instantiation. Written out as a specialization or a copy, it is an error, so such a function
// is printed without `constexpr`.
bool printsConstexpr(const clang::FunctionDecl* func);

// Instantiates the move or copy constructors hoist-calls will need when it moves a call's
// result of class type into a variable. In the input the result is built in place, without a
// copy or a move, so clang may never have instantiated the constructor.
void instantiateConstructorsOfValues(clang::ASTContext& ctx, clang::Sema& sema);

// How many function templates with the name of the specialization @p func can produce its type
// by deduction. With @p with_arguments, deduction starts from @p func's template arguments.
// When exactly one template can, the specialization can be declared without its template
// arguments, eg. `template<> int abs(int)` rather than `template<> int abs<int>(int)`.
unsigned countDeducedTemplates(clang::Sema& sema, clang::FunctionDecl* func, bool with_arguments);

// Whether @p func can be given a copy, or with @p specialized, written as an explicit
// specialization. It needs a body, must not be inside another function, and its types and class
// must be nameable at the end of the TU. A copy must also be callable as a free function. eg.
// `vector<int>::size()` can be copied, a constructor can only be specialized, and a member of a
// local class can be neither.
bool isCopyable(const clang::FunctionDecl* func, bool specialized = false);

// Whether @p func is always written as an explicit specialization, never copied: a constructor
// or a destructor, which can't be a free function, or a virtual function, whose specialization
// the vtable needs anyway.
bool isAlwaysSpecialized(const clang::FunctionDecl* func);

}  // namespace invisible
