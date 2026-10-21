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

// Prints the statements of the definitions DeclBeforeDef writes at the end of the TU where
// clang's statement printer writes code that doesn't compile again. DeclBeforeDef prints every
// deferred body and every specialization from the AST, so each of these shortcomings would
// otherwise break the output, eg.
//
//     auto [a, b] = pair;          printed by clang as   auto = pair;
//     if constexpr (sizeof(T) > 4) printed by clang as   if (sizeof(int) > 4)   (both branches compiled)
//     long double(x)               printed by clang as   long double(x)         (not C++)
//
// clang calls handledStmt for every statement it prints; returning true means the statement
// was printed here, false lets clang print it.
class DeclBeforeDefPrinterHelper : public clang::PrinterHelper {
  public:
    // @p policy is the policy of the text being printed, adjusted for C++ and printing the
    // definitions of tags, as the code around the statements is.
    DeclBeforeDefPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy);

    // Prints @p stmt to @p stream if it is one clang prints wrong, see the class comment.
    bool handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) override;

  private:
    clang::ASTContext& m_ctx;
    clang::PrintingPolicy m_policy;
};

// The form @p comparison was rewritten to, eg. `!(a == b)` for `a != b`, when it calls a function
// of @p copies, else null.
clang::Expr* rewrittenForm(clang::ASTContext& ctx, clang::CXXRewrittenBinaryOperator* comparison,
                           const std::map<const clang::FunctionDecl*, std::string>& copies);

// The printer of monomorphization: prints a call of a function that was given
// a copy as a call of the copy, a free function taking the object of a member first, eg.
//
//     v.size()        => size__mono1a2b3c4d5e(__builtin_addressof(v))
//     make().get()    => get__mono0f0e0d0c0b(__mono_addr(make()))
//     a + b           => operator__mono...(__builtin_addressof(a), b)    (a member operator+)
//
// so that what is deferred, and the copies themselves, call the copies rather than the
// templates, which monomorphization reduces to declarations. See DeclBeforeDefVisitor.
class MonoPrinterHelper : public DeclBeforeDefPrinterHelper {
  public:
    // @p copies is the name of the copy of each function given one, by canonical declaration.
    MonoPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy,
                      const std::map<const clang::FunctionDecl*, std::string>& copies);

    // Prints a call of a copied function as a call of its copy, a constant condition as its
    // value and the static_asserts of an instantiation as empty statements; anything else as
    // DeclBeforeDefPrinterHelper does.
    bool handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) override;

  private:
    clang::ASTContext& m_ctx;
    clang::PrintingPolicy m_policy;
    const std::map<const clang::FunctionDecl*, std::string>& m_copies;
    // how many constant expressions are being printed: the calls in one stay calls of the
    // originals, which the compiler evaluates, and a copy, called at run time, isn't for that
    unsigned m_constant = 0;
};

}  // namespace invisible
