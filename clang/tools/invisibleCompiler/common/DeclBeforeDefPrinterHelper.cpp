#include "common/DeclBeforeDefPrinterHelper.h"
#include "helpers.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/JSON.h"
#include "clang/AST/TemplateBase.h"

#include <string>
#include <queue>
#include <stack>

namespace invisible {

// ─── DeclBeforeDefPrinterHelper ──────────────────────────────────────────────

DeclBeforeDefPrinterHelper::DeclBeforeDefPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy)
    : m_ctx(ctx),
      m_policy(policy) {
        if (m_ctx.getLangOpts().CPlusPlus)
            m_policy.adjustForCPlusPlus();
        m_policy.IncludeTagDefinition = true;
      }

bool DeclBeforeDefPrinterHelper::handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) {
    if (auto* gcc_asm = llvm::dyn_cast<clang::GCCAsmStmt>(stmt)) {
        std::string asm_stmt;
        llvm::raw_string_ostream asm_out(asm_stmt);
        gcc_asm->printPretty(asm_out, nullptr, m_policy);
        asm_out.flush();

        asm_stmt.replace(0, 3, "__asm__");
        stream << asm_stmt << ";";
        return true;
    }

    // clang asserts printing an atomic builtin of only a pointer and an order, eg.
    // `__atomic_test_and_set(p, order)`, looking for a value
    if (auto* atomic = llvm::dyn_cast<clang::AtomicExpr>(stmt);
        atomic && atomic->getNumSubExprs() == 2 && atomic->getOp() != clang::AtomicExpr::AO__c11_atomic_init &&
        atomic->getOp() != clang::AtomicExpr::AO__opencl_atomic_init) {
        switch (atomic->getOp()) {
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS) \
    case clang::AtomicExpr::AO##ID:     \
        stream << #ID "(";              \
        break;
#include "clang/Basic/Builtins.inc"
        }
        atomic->getPtr()->printPretty(stream, this, m_policy);
        stream << ", ";
        atomic->getOrder()->printPretty(stream, this, m_policy);
        stream << ")";
        return true;
    }

    // clang prints a structured binding declaration as `auto = init`, and so is the
    // variable of a range-based for loop
    auto print_bindings = [&stream](const clang::DecompositionDecl* decomposition) {
        clang::QualType written = decomposition->getTypeSourceInfo()->getType();
        std::string reference;
        if (const auto* ref = written->getAs<clang::ReferenceType>()) {
            reference = llvm::isa<clang::RValueReferenceType>(ref) ? "&&" : "&";
            written = ref->getPointeeTypeAsWritten();
        }
        if (written.isConstQualified()) stream << "const ";
        if (written.isVolatileQualified()) stream << "volatile ";
        stream << "auto" << reference << " [";
        llvm::interleave(decomposition->bindings(), stream,
                         [&stream](const clang::BindingDecl* binding) { stream << binding->getName(); }, ", ");
        stream << "]";
    };
    if (auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(stmt); decl_stmt && decl_stmt->isSingleDecl()) {
        if (auto* decomposition = llvm::dyn_cast<clang::DecompositionDecl>(decl_stmt->getSingleDecl())) {
            print_bindings(decomposition);
            stream << " = ";
            decomposition->getInit()->printPretty(stream, this, m_policy);
            stream << ";";
            return true;
        }
    }
    if (auto* range_for = llvm::dyn_cast<clang::CXXForRangeStmt>(stmt); range_for && !range_for->getInit()) {
        if (auto* decomposition = llvm::dyn_cast<clang::DecompositionDecl>(range_for->getLoopVariable())) {
            stream << "for (";
            print_bindings(decomposition);
            stream << " : ";
            range_for->getRangeInit()->printPretty(stream, this, m_policy);
            stream << ") ";
            range_for->getBody()->printPretty(stream, this, m_policy);
            return true;
        }
    }

    // `T(x)` of a type named by more than one word, eg. `long double`, is a C-style
    // cast. FunctionalCastToCStyleCast doesn't see a materialized default argument
    // or a member initializer.
    if (auto* cast = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(stmt);
        cast && !cast->isListInitialization() && !cast->getType()->isDependentType()) {
        if (cast->getType().getCanonicalType().getAsString(m_policy).find(' ') != std::string::npos) {
            stream << "(" << cast->getType().getAsString(m_policy) << ")(";
            cast->getSubExprAsWritten()->printPretty(stream, this, m_policy);
            stream << ")";
            return true;
        }
    }

    // an implicit conversion by a constructor is printed as its arguments, which
    // are more than the one converted once its default arguments are materialized
    if (auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(stmt);
        construct && !llvm::isa<clang::CXXTemporaryObjectExpr>(construct) &&
        construct->getParenOrBraceRange().isInvalid() && !construct->isListInitialization() &&
        construct->getNumArgs() > 1 && !llvm::isa<clang::CXXDefaultArgExpr>(construct->getArg(1))) {
        construct->getType().getUnqualifiedType().print(stream, m_policy);
        stream << "(";
        for (unsigned i = 0; i < construct->getNumArgs() && !llvm::isa<clang::CXXDefaultArgExpr>(construct->getArg(i)); ++i) {
            if (i) stream << ", ";
            construct->getArg(i)->printPretty(stream, this, m_policy);
        }
        stream << ")";
        return true;
    }

    // clang prints `if constexpr` as a plain `if`, whose discarded branches don't compile
    if (auto* if_stmt = llvm::dyn_cast<clang::IfStmt>(stmt); if_stmt && if_stmt->isConstexpr()) {
        stream << "if constexpr (";
        if (clang::Stmt* init = if_stmt->getInit()) {
            init->printPretty(stream, this, m_policy);
            if (!llvm::isa<clang::DeclStmt>(init)) stream << ";";
            stream << " ";
        }
        if_stmt->getCond()->printPretty(stream, this, m_policy);
        stream << ") ";
        std::string then_text = "{}";
        if (clang::Stmt* then = if_stmt->getThen()) {
            llvm::raw_string_ostream then_out(then_text);
            then_text.clear();
            then->printPretty(then_out, this, m_policy);
        }
        stream << llvm::StringRef(then_text).rtrim("\n");
        if (clang::Stmt* otherwise = if_stmt->getElse()) {
            stream << " else ";
            otherwise->printPretty(stream, this, m_policy);
        }
        return true;
    }

    // `sizeof...(Ts)` in an instantiation is printed as its value, a specialization
    // doesn't have the pack.
    if (auto* pack_size = llvm::dyn_cast<clang::SizeOfPackExpr>(stmt); pack_size && !pack_size->isValueDependent()) {
        llvm::APSInt length(llvm::APInt(m_ctx.getTypeSize(pack_size->getType()), pack_size->getPackLength()), true);
        stream << length << integerSuffix(m_ctx, length, pack_size->getType());
        return true;
    }

    return false;
}

}  // namespace invisible
