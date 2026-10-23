#pragma once

#include "clang/AST/ASTContext.h"
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
#include <vector>
#include <cassert>

namespace invisible {

// --- DeclBeforeDefPrinterHelper -------------------------------------------

// Prints the statements that clang's statement printer prints as code that doesn't compile,
// for the definitions DeclBeforeDef writes at the end of the TU. DeclBeforeDef prints every
// moved body and every specialization from the AST, so each of these mistakes would break the
// output, eg.
//
//     auto [a, b] = pair;          printed by clang as   auto = pair;
//     if constexpr (sizeof(T) > 4) printed by clang as   if (sizeof(int) > 4)   (both branches compiled)
//     ld(x), ld = long double      printed by clang as   long double(x)         (doesn't parse)
//
// clang calls handledStmt for every statement it prints; returning true means the statement
// was printed here, false lets clang print it.
class DeclBeforeDefPrinterHelper : public clang::PrinterHelper {
  public:
    // @p policy: the policy of the code around the statements. Like that code, the statements are
    // printed with C++ spelling and with the definitions of the classes and enums they declare.
    DeclBeforeDefPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy);

    // Prints @p stmt to @p stream if it is one clang prints wrong, see the class comment.
    bool handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) override;

  private:
    clang::ASTContext& m_ctx;
    clang::PrintingPolicy m_policy;
};

// The expression clang rewrote @p comparison into, eg. `!(a == b)` for `a != b`, when it calls a
// function that has a copy in @p copies. Otherwise null.
clang::Expr* rewrittenForm(clang::ASTContext& ctx, clang::CXXRewrittenBinaryOperator* comparison,
                           const std::map<const clang::FunctionDecl*, std::string>& copies);

// The printer of monomorphization: prints a call of a function that has a copy as a call of the
// copy. The copy is a free function, so the object of a member function is passed first, as a
// pointer, eg.
//
//     v.size()        => size__mono1a2b3c4d5e(__builtin_addressof(v))
//     make().get()    => get__mono0f0e0d0c0b(__mono_addr(make()))
//     a + b           => operator__mono...(__builtin_addressof(a), b)    (a member operator+)
//
// This way the moved definitions and the copies call the copies, not the templates, which
// monomorphization reduces to declarations. See DeclBeforeDefVisitor.
class MonoPrinterHelper : public DeclBeforeDefPrinterHelper {
  public:
    // @p copies is the name of the copy of each function given one, by canonical declaration.
    MonoPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy,
                      const std::map<const clang::FunctionDecl*, std::string>& copies);

    // Prints a call of a copied function as a call of its copy, a boolean constant expression as
    // `true` or `false`, and a statement of only static_asserts as `;`. Anything else is printed
    // as DeclBeforeDefPrinterHelper does.
    bool handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) override;

  private:
    clang::ASTContext& m_ctx;
    clang::PrintingPolicy m_policy;
    const std::map<const clang::FunctionDecl*, std::string>& m_copies;
    // how deep the printer is inside constant expressions. Calls inside one keep calling the
    // original function: the compiler evaluates the call where it is written, and only the
    // original, which KeepInPlaceVisitor keeps in place, is defined there.
    unsigned m_constant = 0;
};

}  // namespace invisible
