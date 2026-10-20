#include "preprocess/short_circuit_operator_to_ir/ShortCircuitOperatorToIRVisitor.h"
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

namespace {

// Whether @p expr is a constant expression, which is left as written: it may have to
// be one, eg. initializing a constexpr variable, and has the same value either way.
bool isConstant(const clang::Expr* expr, const clang::ASTContext& ctx) {
    if (expr->isValueDependent() || expr->isTypeDependent()) return false;
    // C has integer constant expressions only
    return ctx.getLangOpts().CPlusPlus ? expr->isCXX11ConstantExpr(ctx) : expr->isIntegerConstantExpr(ctx);
}

}  // namespace

// --- ShortCircuitOperatorToIRVisitor ----------------------------------------------

ShortCircuitOperatorToIRVisitor::ShortCircuitOperatorToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, size_t& current_index)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(codePolicy(ctx)), m_current_index(current_index), m_last_update_loc(0) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.PrintAsCanonical = true;
    m_policy.SuppressUnwrittenScope = true;
}

bool ShortCircuitOperatorToIRVisitor::didConvertToIR() {
    return m_last_update_loc != 0;
}

bool ShortCircuitOperatorToIRVisitor::VisitBinaryOperator(clang::BinaryOperator* binary_op) {
    if (!binary_op->isLogicalOp()) return true;
    if (!containsCall(binary_op->getLHS()) && !containsCall(binary_op->getRHS())) return true;
    if (isConstant(binary_op, m_ctx)) return true;
    /**
     * `.. expr1 && expr2 ..`                        `.. expr1 || expr2 ..`
     * is transformed to                             is transformed to
     * bool __short_circuit_result_#id = false;      bool __short_circuit_result_#id = true;
     * if (expr1) {                                  if (!(expr1)) {
     *     if (expr2) {                                  if (!(expr2)) {
     *         __short_circuit_result_#id = true;            __short_circuit_result_#id = false;
     *     }                                             }
     * }                                             }
     * .. __short_circuit_result_#id ..              .. __short_circuit_result_#id ..
     * The result has the type of the expression: bool in C++, so that `show(a && b)` picks the
     * same overload, and int, 1 or 0, in C.
     */
    // one operator per statement and iteration, see m_last_update_loc
    clang::SourceLocation nearest_compound_stmt = findInsertionLoc(binary_op);
    if(nearest_compound_stmt.isInvalid() || nearest_compound_stmt.getRawEncoding() == m_last_update_loc) return true;
    else m_last_update_loc = nearest_compound_stmt.getRawEncoding();

    bool is_and = binary_op->getOpcode() == clang::BO_LAnd;
    bool is_bool = binary_op->getType()->isBooleanType();
    std::string truth = is_bool ? "true" : "1", falsehood = is_bool ? "false" : "0";
    auto test = [&](const clang::Expr* operand) {
        std::string text;
        llvm::raw_string_ostream text_out(text);
        operand->printPretty(text_out, nullptr, m_policy);
        return is_and ? text : "!(" + text + ")";
    };

    std::string result = "__short_circuit_result_" + std::to_string(m_current_index++);
    std::string ir_form{};
    llvm::raw_string_ostream out(ir_form);
    out << binary_op->getType().getAsString(m_policy) << " " << result << " = " << (is_and ? falsehood : truth) << ";\n"
        << "if (" << test(binary_op->getLHS()) << ") {\n"
        << "    if (" << test(binary_op->getRHS()) << ") {\n"
        << "        " << result << " = " << (is_and ? truth : falsehood) << ";\n}\n}\n";

    // an operator that starts its statement, `a() && b();`, is replaced by the if and the result,
    // two edits at one location being one; otherwise the if goes before the statement
    if (nearest_compound_stmt == binary_op->getSourceRange().getBegin()) {
        replaceExpr(m_rewriter, binary_op->getSourceRange(), ir_form + result);
    } else {
        m_rewriter.InsertTextBefore(nearest_compound_stmt, ir_form);
        replaceExpr(m_rewriter, binary_op->getSourceRange(), result);
    }
    return true;
}

bool ShortCircuitOperatorToIRVisitor::VisitConditionalOperator(clang::ConditionalOperator* conditional_op) {
    if (!containsCall(conditional_op->getLHS()) && !containsCall(conditional_op->getRHS())) return true;
    if (isConstant(conditional_op, m_ctx)) return true;
    /**
     * .. `cond ? expr1 : expr2` ..
     * is transformed to
     * T __short_circuit_result_#id;
     * if (cond) {
     *     __short_circuit_result_#id = expr1;
     * } else {
     *     __short_circuit_result_#id = expr2;
     * }
     * .. __short_circuit_result_#id ..
     * T is the type of the whole expression, eg. double for `c ? f() : 2.5`. When the expression
     * names an object rather than a value, eg. `(c ? a : b) = 5`, the result holds its address
     * instead, `T *__short_circuit_result_#id = __builtin_addressof(expr1);`, and is used as
     * `(*__short_circuit_result_#id)`, so that the assignment still writes to `a` or `b`.
     */
    clang::QualType type = conditional_op->getType();
    bool by_address = conditional_op->isGLValue() && m_ctx.getLangOpts().CPlusPlus;
    // a bit-field has no address
    if (by_address && conditional_op->refersToBitField()) return true;

    clang::SourceLocation nearest_compound_stmt = findInsertionLoc(conditional_op);
    if(nearest_compound_stmt.isInvalid() || nearest_compound_stmt.getRawEncoding() == m_last_update_loc) return true;
    else m_last_update_loc = nearest_compound_stmt.getRawEncoding();

    auto print = [this](const clang::Expr* expr) {
        std::string text;
        llvm::raw_string_ostream text_out(text);
        expr->printPretty(text_out, nullptr, m_policy);
        return text;
    };
    std::string cond = print(conditional_op->getCond());
    std::string lexpr = print(conditional_op->getLHS());
    std::string rexpr = print(conditional_op->getRHS());

    std::string ir_form{};
    llvm::raw_string_ostream out(ir_form);
    std::string short_circuit_result;
    if (type->isVoidType()) {
        out << "if (" << cond << ") {\n" << lexpr << ";\n} else {\n" << rexpr << ";\n}\n";
        short_circuit_result = "(void)0";
    } else {
        std::string result = "__short_circuit_result_" + std::to_string(m_current_index++);
        auto value = [by_address](const std::string& operand) {
            return by_address ? "__builtin_addressof(" + operand + ")" : operand;
        };
        (by_address ? m_ctx.getPointerType(type) : type.getUnqualifiedType()).print(out, m_policy, result);
        out << ";\nif (" << cond << ") {\n" << result << " = " << value(lexpr) << ";\n} else {\n"
            << result << " = " << value(rexpr) << ";\n}\n";
        std::string type_name = type.getAsString(m_policy);
        if (conditional_op->isXValue()) short_circuit_result = "static_cast<" + type_name + " &&>(*" + result + ")";
        else if (by_address) short_circuit_result = "(*" + result + ")";
        // the temporary the expression was is moved from, as hoist-calls does
        else if (m_ctx.getLangOpts().CPlusPlus) short_circuit_result = "static_cast<" + type_name + " &&>(" + result + ")";
        else short_circuit_result = result;
    }

    if (nearest_compound_stmt == conditional_op->getSourceRange().getBegin()) {
        replaceExpr(m_rewriter, conditional_op->getSourceRange(), ir_form + short_circuit_result);
    } else {
        m_rewriter.InsertTextBefore(nearest_compound_stmt, ir_form);
        replaceExpr(m_rewriter, conditional_op->getSourceRange(), short_circuit_result);
    }

    return true;
}

// The start of the statement of the nearest block holding @p expr, eg. the return of
// `return c ? f() : g();`, before which the if is inserted. Invalid outside a block, eg. in a
// member initializer, where the expression is then left.
clang::SourceLocation ShortCircuitOperatorToIRVisitor::findInsertionLoc(const clang::Expr* expr) {
    const clang::Stmt* statement = expr;
    for (auto parents = m_ctx.getParents(*expr); !parents.empty(); parents = m_ctx.getParents(parents[0])) {
        if (parents[0].get<clang::CompoundStmt>()) return statement->getBeginLoc();
        if (const auto* stmt = parents[0].get<clang::Stmt>()) statement = stmt;
    }
    return {};
}

}  // namespace invisible
