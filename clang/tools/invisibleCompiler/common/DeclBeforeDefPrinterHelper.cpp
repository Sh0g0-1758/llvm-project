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

// --- DeclBeforeDefPrinterHelper ----------------------------------------------

// the statements are printed as the code around them is: C++ spelling, with the definitions of
// the tags they declare, eg. a local `struct Node { int v; };` keeps its members
DeclBeforeDefPrinterHelper::DeclBeforeDefPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy)
    : m_ctx(ctx),
      m_policy(policy) {
        if (m_ctx.getLangOpts().CPlusPlus)
            m_policy.adjustForCPlusPlus();
        m_policy.IncludeTagDefinition = true;
      }

bool DeclBeforeDefPrinterHelper::handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) {
    // clang prints inline assembly as `asm(...)`, which strict C modes don't know as a keyword, eg.
    // `asm volatile("pause")` in a -std=c11 TU. `__asm__` is a keyword in every mode.
    if (auto* gcc_asm = llvm::dyn_cast<clang::GCCAsmStmt>(stmt)) {
        std::string asm_stmt;
        llvm::raw_string_ostream asm_out(asm_stmt);
        gcc_asm->printPretty(asm_out, nullptr, m_policy);
        asm_out.flush();

        asm_stmt.replace(0, 3, "__asm__");
        stream << asm_stmt << ";";
        return true;
    }

    // clang's printer asserts on an atomic builtin that takes only a pointer and an order, eg.
    // `__atomic_test_and_set(p, order)`, as it expects a value operand too
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

    // clang prints a structured binding declaration without its names, `auto = init`, also as the
    // variable of a range-based for, eg. `for (auto& [key, value] : map)` becomes
    // `for (auto & : map)`. The bindings are printed as written: `auto& [key, value]`.
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
    // clang prints a declaration of several variables of a deduced type with the type repeated,
    // `auto a = x, auto b = y`, also in the init of a for, eg. `for (auto i = 0, n = size(); ...)`.
    // The type is printed once: `auto i = 0, n = size()`.
    auto print_deduced = [this](const clang::Stmt* init, llvm::raw_ostream& out) {
        const auto* decl_stmt = llvm::dyn_cast_or_null<clang::DeclStmt>(init);
        if (!decl_stmt || decl_stmt->isSingleDecl()) return false;
        const auto* first = llvm::dyn_cast<clang::VarDecl>(*decl_stmt->decl_begin());
        if (!first || !first->getTypeSourceInfo() || !first->getTypeSourceInfo()->getType()->getContainedAutoType() ||
            !llvm::all_of(decl_stmt->decls(), [first](const clang::Decl* decl) {
                const auto* var = llvm::dyn_cast<clang::VarDecl>(decl);
                return var && var->getType() == first->getType() && var->getInitStyle() == clang::VarDecl::CInit;
            }))
            return false;
        first->print(out, m_policy);
        for (const clang::Decl* decl : llvm::drop_begin(decl_stmt->decls())) {
            const auto* var = llvm::cast<clang::VarDecl>(decl);
            out << ", " << var->getName();
            if (const clang::Expr* value = var->getInit()) {
                out << " = ";
                value->printPretty(out, this, m_policy);
            }
        }
        return true;
    };
    if (llvm::isa<clang::DeclStmt>(stmt) && print_deduced(stmt, stream)) {
        stream << ";";
        return true;
    }
    if (auto* for_stmt = llvm::dyn_cast<clang::ForStmt>(stmt); for_stmt && !for_stmt->getConditionVariable()) {
        std::string init;
        llvm::raw_string_ostream init_out(init);
        if (print_deduced(for_stmt->getInit(), init_out)) {
            stream << "for (" << init << "; ";
            if (clang::Expr* cond = for_stmt->getCond()) cond->printPretty(stream, this, m_policy);
            stream << "; ";
            if (clang::Expr* inc = for_stmt->getInc()) inc->printPretty(stream, this, m_policy);
            stream << ") ";
            for_stmt->getBody()->printPretty(stream, this, m_policy);
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

    // A function style cast to a type of several words, eg. `long double(x)`, doesn't parse, so it is
    // printed as a C style cast, `((long double)(x))`. FunctionalCastToCStyleCast rewrites these in
    // the AST, but doesn't reach a constructor's member initializers, which are printed here. A space
    // inside template arguments doesn't count, eg. `cd(x)` of `typedef std::pair<int, int> cd` is
    // kept. The whole cast is parenthesized, as `(T)(x).m` would cast `(x).m`.
    if (auto* cast = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(stmt);
        cast && !cast->isListInitialization() && !cast->getType()->isDependentType()) {
        auto several_words = [](const std::string& name) {
            int depth = 0;
            for (char c : name) {
                if (c == '<' || c == '(') ++depth;
                else if (c == '>' || c == ')') --depth;
                else if (c == ' ' && depth == 0) return true;
            }
            return false;
        };
        if (several_words(cast->getType().getAsString(m_policy)) ||
            several_words(cast->getType().getCanonicalType().getAsString(m_policy))) {
            stream << "((" << cast->getType().getAsString(m_policy) << ")(";
            cast->getSubExprAsWritten()->printPretty(stream, this, m_policy);
            stream << "))";
            return true;
        }
    }

    // a call of a member function with an explicit object parameter, `void f(this S& self, int)`,
    // is a plain call with the object first in the AST, which clang prints as `f(*this, 1)` for
    // `this->f(1)`. It is printed as a member call, `(*this).f(1)`.
    if (auto* call = llvm::dyn_cast<clang::CallExpr>(stmt);
        call && call->getStmtClass() == clang::Stmt::CallExprClass && call->getNumArgs() > 0) {
        auto* callee = llvm::dyn_cast<clang::DeclRefExpr>(call->getCallee()->IgnoreImpCasts());
        auto* method = callee ? llvm::dyn_cast<clang::CXXMethodDecl>(callee->getDecl()) : nullptr;
        if (method && method->isExplicitObjectMemberFunction()) {
            stream << "(";
            call->getArg(0)->printPretty(stream, this, m_policy);
            stream << ").";
            callee->printPretty(stream, this, m_policy);
            stream << "(";
            for (unsigned i = 1; i < call->getNumArgs() && !llvm::isa<clang::CXXDefaultArgExpr>(call->getArg(i)); ++i) {
                if (i > 1) stream << ", ";
                call->getArg(i)->printPretty(stream, this, m_policy);
            }
            stream << ")";
            return true;
        }
    }

    // `p->~T()` of a scalar T does nothing, and with eg. `T = int *` clang prints `p->~T *()`, which
    // doesn't parse. It is printed as `(void)(p)`, which still evaluates `p`.
    if (auto* call = llvm::dyn_cast<clang::CallExpr>(stmt)) {
        if (auto* pseudo = llvm::dyn_cast<clang::CXXPseudoDestructorExpr>(call->getCallee()->IgnoreParens())) {
            stream << "(void)(";
            pseudo->getBase()->printPretty(stream, this, m_policy);
            stream << ")";
            return true;
        }
    }

    // a requires expression in an instantiation is printed as its value, `true` or `false`: a
    // requirement that failed has no expression left to print, eg. `requires { t.size(); }` with
    // `T = int` becomes `false`
    if (auto* requires_expr = llvm::dyn_cast<clang::RequiresExpr>(stmt); requires_expr && !requires_expr->isValueDependent()) {
        stream << (requires_expr->isSatisfied() ? "true" : "false");
        return true;
    }

    // clang prints `if constexpr` as a plain `if`, whose discarded branches don't compile, eg.
    // `if constexpr (is_pointer_v<T>) return *t;` of `T = int` would dereference an int
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

    // `sizeof...(Ts)` in an instantiation is printed as its value, as the printed specialization has
    // no pack, eg. `sizeof...(Ts)` of `f<int, char>` becomes `2UL`
    if (auto* pack_size = llvm::dyn_cast<clang::SizeOfPackExpr>(stmt); pack_size && !pack_size->isValueDependent()) {
        llvm::APSInt length(llvm::APInt(m_ctx.getTypeSize(pack_size->getType()), pack_size->getPackLength()), true);
        stream << length << integerSuffix(m_ctx, length, pack_size->getType());
        return true;
    }

    return false;
}

// --- MonoPrinterHelper ----------------------------------------------
//
// What is deferred calls the copies, see MonoPrinterHelper in the header: the AST still calls
// the instantiations, which are only declared at the end, so the calls are respelled as the
// copies are printed.

// the same policy as the base printer, kept here for the calls of copies it prints itself
MonoPrinterHelper::MonoPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy,
                                     const std::map<const clang::FunctionDecl*, std::string>& copies)
    : DeclBeforeDefPrinterHelper(ctx, policy), m_ctx(ctx), m_policy(policy), m_copies(copies) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
}

bool MonoPrinterHelper::handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) {
    // a constant expression is evaluated by the compiler, which needs the definition
    // of what it calls before it
    if (auto* constant = llvm::dyn_cast<clang::ConstantExpr>(stmt)) {
        // a condition is printed as its value, the names in it may not be found where it is printed
        if (constant->getResultStorageKind() != clang::ConstantResultStorageKind::None && constant->getType()->isBooleanType()) {
            stream << (constant->getResultAsAPSInt().getBoolValue() ? "true" : "false");
            return true;
        }
        ++m_constant;
        constant->getSubExpr()->printPretty(stream, this, m_policy);
        --m_constant;
        return true;
    }
    // the static_asserts of an instantiation held when it was instantiated, so they are left out
    if (auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(stmt);
        decl_stmt && llvm::all_of(decl_stmt->decls(), [](const clang::Decl* decl) { return llvm::isa<clang::StaticAssertDecl>(decl); })) {
        stream << ";";
        return true;
    }
    // a call of a function that has a copy, outside a constant expression
    auto* call = llvm::dyn_cast<clang::CallExpr>(stmt);
    const clang::FunctionDecl* callee = call ? call->getDirectCallee() : nullptr;
    auto found = callee && !m_constant ? m_copies.find(callee->getCanonicalDecl()) : m_copies.end();
    if (found == m_copies.end())
        return DeclBeforeDefPrinterHelper::handledStmt(stmt, stream);

    // the object of a member function
    const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(callee);
    clang::Expr* object = nullptr;
    bool arrow = false;
    unsigned first = 0;
    if (llvm::isa<clang::CXXOperatorCallExpr>(call) && method) {
        if (method->isInstance())
            object = call->getArg(0);
        first = 1;
    } else if (method && method->isInstance()) {
        auto* member = llvm::dyn_cast<clang::MemberExpr>(call->getCallee()->IgnoreParens());
        if (!member)
            return DeclBeforeDefPrinterHelper::handledStmt(stmt, stream);
        object = member->getBase();
        arrow = member->isArrow();
    }

    // `obj.f(a)` becomes `f__mono...(__builtin_addressof(obj), a)` and `ptr->f(a)` becomes
    // `f__mono...(ptr, a)`. A temporary has no address of its own: `make().f()` becomes
    // `f__mono...(__mono_addr(make()))`, a helper returning the address of its argument, which
    // lives until the end of the full expression.
    stream << found->second << "(";
    const char* separator = "";
    if (object) {
        if (!arrow)
            stream << (object->isLValue() && !object->IgnoreParenImpCasts()->isPRValue()
                           ? "__builtin_addressof(" : "__mono_addr(");
        object->printPretty(stream, this, m_policy);
        if (!arrow)
            stream << ")";
        separator = ", ";
    }
    for (unsigned i = first; i < call->getNumArgs(); ++i) {
        clang::Stmt* arg = call->getArg(i);
        // a default argument is written where the function is declared
        if (auto* default_arg = llvm::dyn_cast<clang::CXXDefaultArgExpr>(arg)) {
            arg = default_arg->getExpr();
            qualifyNonLocalNames(arg, m_ctx);
        }
        stream << separator;
        arg->printPretty(stream, this, m_policy);
        separator = ", ";
    }
    stream << ")";
    return true;
}

}  // namespace invisible
