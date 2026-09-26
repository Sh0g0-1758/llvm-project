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

// ─── ShortCircuitOperatorToIRVisitor ──────────────────────────────────────────────

ShortCircuitOperatorToIRVisitor::ShortCircuitOperatorToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, size_t& current_index)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_current_index(current_index), m_last_update_loc(0) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
}

bool ShortCircuitOperatorToIRVisitor::didConvertToIR() {
    return m_last_update_loc != 0;
}

bool ShortCircuitOperatorToIRVisitor::VisitBinaryOperator(clang::BinaryOperator* binary_op) {
    if(binary_op->getOpcode() == clang::BinaryOperator::Opcode::BO_LAnd) {
        if(!exprHasFuncCall(binary_op->getLHS()) && !exprHasFuncCall(binary_op->getRHS())) return true;
        /**
         * `var = expr1 && expr2;`
         * is transformed to
         * int __short_circuit_result_#id = 0;
         * if (expr1) {
         *     if (expr2) {
         *         __short_circuit_result_#id = 1;
         *     }
         * }
         * var = __short_circuit_result_#id;
         * 
         * Its okay to store the result as an int because any arith ops
         * would anyways convert the bool to an int
         * 
         * Another transformation would be to store the individual
         * results of expr1 and expr2 as variables and then emit
         * the short-circuiting in its IR form as well as it is. 
         * The current design introduced the least amount of variables
         * in another scope and does not introduce another short-circuit
         * operation as well
         */
        clang::SourceLocation nearest_compound_stmt = findInsertionLoc(binary_op);
        if(nearest_compound_stmt.getRawEncoding() == m_last_update_loc) return true;
        else m_last_update_loc = nearest_compound_stmt.getRawEncoding();

        std::string ir_form{};
        llvm::raw_string_ostream out(ir_form);

        std::string lexpr;
        llvm::raw_string_ostream lexpr_out(lexpr);
        binary_op->getLHS()->printPretty(lexpr_out, nullptr, m_policy);
        lexpr_out.flush();

        std::string rexpr;
        llvm::raw_string_ostream rexpr_out(rexpr);
        binary_op->getRHS()->printPretty(rexpr_out, nullptr, m_policy);
        rexpr_out.flush();


        std::string short_circuit_result = "__short_circuit_result_" + std::to_string(m_current_index++);

        out << "int " << short_circuit_result << " = 0" << ";\n";
        out << "if (" << lexpr << ") " << "{\n";
        out << "    if (" << rexpr << ") {\n" << "        " << short_circuit_result << " = 1;\n}\n}\n";
        out.flush();

        if (nearest_compound_stmt == binary_op->getSourceRange().getBegin()) {
            m_rewriter.ReplaceText(binary_op->getSourceRange(), ir_form + short_circuit_result);
        } else {
            m_rewriter.InsertTextBefore(nearest_compound_stmt, ir_form);
            m_rewriter.ReplaceText(binary_op->getSourceRange(), short_circuit_result);
        }
    } else if(binary_op->getOpcode() == clang::BinaryOperator::Opcode::BO_LOr) {
        if(!exprHasFuncCall(binary_op->getLHS()) && !exprHasFuncCall(binary_op->getRHS())) return true;
        /**
         * `var = expr1 || expr2`
         * is transformed to
         * int __short_circuit_result_#id = 1;
         * if (!(expr1)) {
         *     if (!(expr2)) {
         *         __short_circuit_result_#id = 0;
         *     }
         * }
         * var = __short_circuit_result_#id;
         */
        clang::SourceLocation nearest_compound_stmt = findInsertionLoc(binary_op);
        if(nearest_compound_stmt.getRawEncoding() == m_last_update_loc) return true;
        else m_last_update_loc = nearest_compound_stmt.getRawEncoding();

        std::string ir_form{};
        llvm::raw_string_ostream out(ir_form);

        std::string lexpr;
        llvm::raw_string_ostream lexpr_out(lexpr);
        binary_op->getLHS()->printPretty(lexpr_out, nullptr, m_policy);
        lexpr_out.flush();

        std::string rexpr;
        llvm::raw_string_ostream rexpr_out(rexpr);
        binary_op->getRHS()->printPretty(rexpr_out, nullptr, m_policy);
        rexpr_out.flush();


        std::string short_circuit_result = "__short_circuit_result_" + std::to_string(m_current_index++);

        out << "int " << short_circuit_result << " = 1" << ";\n";
        out << "if (!(" << lexpr << ")) " << "{\n";
        out << "    if (!(" << rexpr << ")) {\n" << "        " << short_circuit_result << " = 0;\n}\n}\n";
        out.flush();

        if (nearest_compound_stmt == binary_op->getSourceRange().getBegin()) {
            m_rewriter.ReplaceText(binary_op->getSourceRange(), ir_form + short_circuit_result);
        } else {
            m_rewriter.InsertTextBefore(nearest_compound_stmt, ir_form);
            m_rewriter.ReplaceText(binary_op->getSourceRange(), short_circuit_result);
        }
    }
    return true;
}

bool ShortCircuitOperatorToIRVisitor::VisitConditionalOperator(clang::ConditionalOperator* conditional_op) {
    if(!exprHasFuncCall(conditional_op->getLHS()) && !exprHasFuncCall(conditional_op->getRHS())) return true;
    /**
     * .. `cond ? expr1 : expr2` ..
     * is transformed to
     * T __short_circuit_result_#id = 1;
     * if (cond) {
     *     __short_circuit_result_#id = expr1;
     * } else {
     *     __short_circuit_result_#id = expr2;
     * }
     * .. __short_circuit_result_#id ..
     */
    clang::SourceLocation nearest_compound_stmt = findInsertionLoc(conditional_op);
    if(nearest_compound_stmt.getRawEncoding() == m_last_update_loc) return true;
    else m_last_update_loc = nearest_compound_stmt.getRawEncoding();

    std::string ir_form{};
    llvm::raw_string_ostream out(ir_form);

    std::string lexpr;
    llvm::raw_string_ostream lexpr_out(lexpr);
    conditional_op->getLHS()->printPretty(lexpr_out, nullptr, m_policy);
    lexpr_out.flush();

    std::string rexpr;
    llvm::raw_string_ostream rexpr_out(rexpr);
    conditional_op->getRHS()->printPretty(rexpr_out, nullptr, m_policy);
    rexpr_out.flush();

    std::string cond;
    llvm::raw_string_ostream cond_out(cond);
    conditional_op->getCond()->printPretty(cond_out, nullptr, m_policy);
    cond_out.flush();

    bool isVoidType = conditional_op->getLHS()->getType()->isVoidType();
    std::string short_circuit_result{};
    if (!isVoidType) {
        short_circuit_result = "__short_circuit_result_" + std::to_string(m_current_index++);
        clang::QualType resType = conditional_op->getLHS()->getType();
        resType.print(out, m_policy, short_circuit_result);
        out << ";\n";
    } else {
        short_circuit_result = "(void)0";
    }

    out << "if (" << cond << ") {\n";
    if (!isVoidType) out << short_circuit_result << " = ";
    out << lexpr << ";\n";
    out << "} else {\n";
    if (!isVoidType) out << short_circuit_result << " = ";
    out << rexpr << ";\n";
    out << "}\n";
    out.flush();

    if (nearest_compound_stmt == conditional_op->getSourceRange().getBegin()) {
        m_rewriter.ReplaceText(conditional_op->getSourceRange(), ir_form + short_circuit_result);
    } else {
        m_rewriter.InsertTextBefore(nearest_compound_stmt, ir_form);
        m_rewriter.ReplaceText(conditional_op->getSourceRange(), short_circuit_result);
    }

    return true;
}

clang::SourceLocation ShortCircuitOperatorToIRVisitor::findInsertionLoc(const clang::ConditionalOperator* conditional_op) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*conditional_op);
    const clang::Stmt* last_stmt = conditional_op;

    while (true) {
        auto parents = m_ctx.getParents(current);
        if (parents.empty())
            return {};
        const auto& parent = parents[0];
        if (parent.get<clang::CompoundStmt>() != nullptr)
            return last_stmt->getBeginLoc();
        if (const auto* stmt = parent.get<clang::Stmt>())
            last_stmt = stmt;

        current = parent;
    }
}

clang::SourceLocation ShortCircuitOperatorToIRVisitor::findInsertionLoc(const clang::BinaryOperator* binary_op) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*binary_op);
    const clang::Stmt* last_stmt = binary_op;

    while (true) {
        auto parents = m_ctx.getParents(current);
        if (parents.empty())
            return {};
        const auto& parent = parents[0];
        if (parent.get<clang::CompoundStmt>() != nullptr)
            return last_stmt->getBeginLoc();
        if (const auto* stmt = parent.get<clang::Stmt>())
            last_stmt = stmt;

        current = parent;
    }
}

bool ShortCircuitOperatorToIRVisitor::exprHasFuncCall(clang::Expr* expr) {
    expr = expr->IgnoreParenImpCasts();
    if(llvm::dyn_cast<clang::CallExpr>(expr)) return true;
    clang::Stmt* stmt = expr;
    std::queue<clang::Stmt*> children;
    for (const clang::Stmt* child : stmt->children()) {
        if (child == nullptr) continue;
        children.push(const_cast<clang::Stmt*>(child));
    }
    bool stmtIsFuncCall = false;
    while(!children.empty()) {
        clang::Stmt* child = children.front();
        if(clang::Expr* expr = llvm::dyn_cast<clang::Expr>(child))
            if(llvm::dyn_cast<clang::CallExpr>(expr->IgnoreParenImpCasts()))
                stmtIsFuncCall = true;
        if(!stmtIsFuncCall) {
            for (const clang::Stmt* grandChild : child->children()) {
                if (grandChild == nullptr) continue;
                children.push(const_cast<clang::Stmt*>(grandChild));
            }
        } else break;
        children.pop();
    }
    return stmtIsFuncCall;
}

}  // namespace invisible
