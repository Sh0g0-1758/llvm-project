#include "invisible.h"
#include "helpers.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/JSON.h"

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
        m_policy.SuppressTagKeyword = false;
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

    return false;
}

// ─── DeclBeforeDefVisitor ──────────────────────────────────────────────

DeclBeforeDefVisitor::DeclBeforeDefVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_custom_printer(ctx, m_policy), m_deferred_globals({}) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool DeclBeforeDefVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody()) return true;
    if (!func->isThisDeclarationADefinition()) return true;

    std::string def;
    llvm::raw_string_ostream def_out(def);
    func->getBody()->printPretty(def_out, &m_custom_printer, m_policy);
    def_out.flush();

    std::string decl;
    llvm::raw_string_ostream decl_out(decl);

    if (!func->hasWrittenPrototype() && func->getNumParams() > 0) {
        std::string proto = func->getNameAsString();
        proto += "(";
        for (unsigned i = 0; i < func->getNumParams(); ++i) {
            if (i > 0) proto += ", ";
            clang::ParmVarDecl* param_decl = func->getParamDecl(i);
            std::string param;
            llvm::raw_string_ostream param_out(param);
            param_decl->getType().print(param_out, m_policy, param_decl->getNameAsString());
            param_out.flush();
            proto += param;
        }
        proto += ")";

        func->getReturnType().print(decl_out, m_policy, proto);
    } else {
        clang::PrintingPolicy declPolicy = m_policy;
        declPolicy.TerseOutput = true;
        func->print(decl_out, declPolicy);
    }
    decl_out.flush();

    m_deferred_definitions.push_back(decl + def);
    m_rewriter.ReplaceText(func->getSourceRange(), decl + ";");
    return true;
}

std::string DeclBeforeDefVisitor::getDeferredDefinitions() {
    for(const auto& it : m_deferred_globals) {
        clang::SourceRange decl_range;
        decl_range.setBegin(it.first);
        decl_range.setEnd(it.second.second);
        m_rewriter.ReplaceText(decl_range, it.second.first);
    }

    for(const auto& it : m_deferred_typedefs) {
        clang::SourceRange decl_range;
        decl_range.setBegin(it.first);
        decl_range.setEnd(it.second.second);
        m_rewriter.ReplaceText(decl_range, it.second.first);
    }

    std::string all_defs;
    size_t total = 1;
    for (const auto& def : m_deferred_definitions)
        total += def.size() + 1;
    all_defs.reserve(total);

    all_defs += '\n';
    for (auto& def : m_deferred_definitions) {
        all_defs += std::move(def);
        all_defs += "\n";
    }

    return all_defs;
}

bool DeclBeforeDefVisitor::VisitVarDecl(clang::VarDecl* glob_decl) {
    if (glob_decl->isLocalVarDeclOrParm()) return true;

    std::string decl;
    llvm::raw_string_ostream decl_out(decl);
    glob_decl->print(decl_out, m_policy);
    decl_out.flush();

    clang::SourceRange decl_range = glob_decl->getSourceRange();

    if (m_deferred_globals.find(decl_range.getBegin()) != m_deferred_globals.end())
        m_deferred_globals[decl_range.getBegin()].first += (decl + ";\n");
    else
        m_deferred_globals[decl_range.getBegin()].first = (decl + ";\n");
    m_deferred_globals[decl_range.getBegin()].second = decl_range.getEnd();
    return true;
}

bool DeclBeforeDefVisitor::VisitEnumDecl(clang::EnumDecl* enum_decl) {
    if (!enum_decl->getDeclContext()->isFileContext()) return true;

    std::string decl;
    llvm::raw_string_ostream decl_out(decl);
    enum_decl->print(decl_out, m_policy);
    decl_out.flush();

    m_rewriter.ReplaceText(enum_decl->getSourceRange(), decl);
    return true;
}

bool DeclBeforeDefVisitor::VisitStaticAssertDecl(clang::StaticAssertDecl* static_assert_decl) {
    if (!static_assert_decl->getDeclContext()->isFileContext()) return true;

    std::string decl;
    llvm::raw_string_ostream decl_out(decl);
    static_assert_decl->print(decl_out, m_policy);
    decl_out.flush();

    m_rewriter.ReplaceText(static_assert_decl->getSourceRange(), decl);
    return true;
}

bool DeclBeforeDefVisitor::VisitTypedefDecl(clang::TypedefDecl* typdef_decl) {
    if (!typdef_decl->getDeclContext()->isFileContext()) return true;

    std::string decl;
    llvm::raw_string_ostream decl_out(decl);
    typdef_decl->print(decl_out, m_policy);
    decl_out.flush();

    clang::SourceRange decl_range = typdef_decl->getSourceRange();

    if (m_deferred_typedefs.find(decl_range.getBegin()) != m_deferred_typedefs.end())
        m_deferred_typedefs[decl_range.getBegin()].first += (decl + ";\n");
    else
        m_deferred_typedefs[decl_range.getBegin()].first = (decl + ";\n");
    m_deferred_typedefs[decl_range.getBegin()].second = decl_range.getEnd();
    return true;
}

// ─── NormalizeMultipleRecordDeclVisitor ──────────────────────────────────────────────

NormalizeMultipleRecordDeclVisitor::NormalizeMultipleRecordDeclVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_custom_printer(ctx, m_policy) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool NormalizeMultipleRecordDeclVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody()) return true;
    if (!func->isThisDeclarationADefinition()) return true;

    std::string def;
    llvm::raw_string_ostream def_out(def);
    func->getBody()->printPretty(def_out, &m_custom_printer, m_policy);
    def_out.flush();

    m_rewriter.ReplaceText(func->getBody()->getSourceRange(), def);
    return true;
}

bool NormalizeMultipleRecordDeclVisitor::VisitVarDecl(clang::VarDecl* glob_decl) {
    if (glob_decl->isLocalVarDeclOrParm()) return true;

    std::string decl;
    llvm::raw_string_ostream decl_out(decl);
    glob_decl->print(decl_out, m_policy);
    decl_out.flush();

    m_rewriter.ReplaceText(glob_decl->getSourceRange(), decl);
    return true;
}

// ─── CanonicalizeVarDeclVisitor ──────────────────────────────────────────────

CanonicalizeVarDeclVisitor::CanonicalizeVarDeclVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), uniqueIndx(0) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool CanonicalizeVarDeclVisitor::VisitVarDecl(clang::VarDecl* decl) {
    std::string canonicalizedName = "___var__" + std::to_string(uniqueIndx++);
    decl->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(canonicalizedName)));
    return true;
}

bool CanonicalizeVarDeclVisitor::VisitParmVarDecl(clang::ParmVarDecl* decl) {
    std::string canonicalizedName = "___var__" + std::to_string(uniqueIndx++);
    decl->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(canonicalizedName)));
    return true;
}

// ─── ShortCircuitOperatorToIRVisitor ──────────────────────────────────────────────

ShortCircuitOperatorToIRVisitor::ShortCircuitOperatorToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, size_t& current_index)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_current_index(current_index), m_last_update_loc(0) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
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

// ─── BreakElseIfVisitor ──────────────────────────────────────────────

BreakElseIfVisitor::BreakElseIfVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool BreakElseIfVisitor::VisitIfStmt(clang::IfStmt* if_stmt) {
    if (!if_stmt->hasElseStorage()) return true;
    if (clang::IfStmt* inner_if_stmt = llvm::dyn_cast<clang::IfStmt>(if_stmt->getElse())) {
        if (!exprHasFuncCall(inner_if_stmt->getCond())) return true;

        m_rewriter.InsertTextBefore(inner_if_stmt->getSourceRange().getBegin(), "{\n");
        m_rewriter.InsertTextBefore(inner_if_stmt->getSourceRange().getEnd().getLocWithOffset(1), "\n}");
    }
    return true;
}

bool BreakElseIfVisitor::exprHasFuncCall(clang::Expr* expr) {
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

// ─── MarkSwitchCaseVisitor ──────────────────────────────────────────────

MarkSwitchCaseVisitor::MarkSwitchCaseVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool MarkSwitchCaseVisitor::VisitCaseStmt(clang::CaseStmt* case_stmt) {
    m_rewriter.InsertTextBefore(case_stmt->getSubStmt()->getSourceRange().getBegin(), ";\n");
    return true;
}

bool MarkSwitchCaseVisitor::VisitDefaultStmt(clang::DefaultStmt* default_stmt) {
    m_rewriter.InsertTextBefore(default_stmt->getSubStmt()->getSourceRange().getBegin(), ";\n");
    return true;
}

// ─── LoopToIRVisitor ──────────────────────────────────────────────

LoopToIRVisitor::LoopToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool LoopToIRVisitor::VisitForStmt(clang::ForStmt* for_stmt) {
    /**
     * `for (init, cond, inc) body`
     * is transformed into
     * for (init;;) {
     *   if (!(cond)) { break; }
     *   body [stripped of surrounding braces]
     *   expr
     * }
     */
    bool hasCond = for_stmt->getCond() != nullptr;
    bool hasInc  = for_stmt->getInc() != nullptr;
    if(!hasCond && !hasInc) return true;
    if(hasCond && !exprHasFuncCall(for_stmt->getCond()) &&
      (hasInc && !exprHasFuncCall(for_stmt->getInc()))) return true;

    std::string cond;
    llvm::raw_string_ostream cond_out(cond);
    if(hasCond) for_stmt->getCond()->printPretty(cond_out, nullptr, m_policy);
    cond_out.flush();

    std::string inc;
    llvm::raw_string_ostream inc_out(inc);
    if(hasInc) {
        for_stmt->getInc()->printPretty(inc_out, nullptr, m_policy);
        inc_out << ";";
    }
    inc_out.flush();

    if (hasCond) m_rewriter.ReplaceText(for_stmt->getCond()->getSourceRange(), "");
    if (hasInc) m_rewriter.ReplaceText(for_stmt->getInc()->getSourceRange(), "");

    std::string cond_wrapped_in_if;
    llvm::raw_string_ostream cond_wrapped_out(cond_wrapped_in_if);
    if(hasCond) cond_wrapped_out << "if (!(" << cond << ")) {\nbreak;\n}\n";
    cond_wrapped_out.flush();
    
    if(hasCond) m_rewriter.InsertTextBefore(for_stmt->getBody()->getSourceRange().getBegin().getLocWithOffset(1), cond_wrapped_in_if);
    if(hasInc) m_rewriter.InsertTextBefore(for_stmt->getBody()->getSourceRange().getEnd(), inc);

    return true;
}

bool LoopToIRVisitor::VisitWhileStmt(clang::WhileStmt* while_stmt) {
    /**
     * `while (cond) body`
     * is transformed into
     * while (1) {
     *   if (!(cond)) { break; }
     *   body [stripped of surrounding braces]
     * }
     */
    if (!exprHasFuncCall(while_stmt->getCond())) return true;

    std::string cond;
    llvm::raw_string_ostream cond_out(cond);
    while_stmt->getCond()->printPretty(cond_out, nullptr, m_policy);
    cond_out.flush();

    m_rewriter.ReplaceText(while_stmt->getCond()->getSourceRange(), "1");

    std::string cond_wrapped_in_if;
    llvm::raw_string_ostream cond_wrapped_out(cond_wrapped_in_if);
    cond_wrapped_out << "if (!(" << cond << ")) {\nbreak;\n}\n";
    cond_wrapped_out.flush();

    m_rewriter.InsertTextBefore(while_stmt->getBody()->getSourceRange().getBegin().getLocWithOffset(1), cond_wrapped_in_if);

    return true;
}

bool LoopToIRVisitor::VisitDoStmt(clang::DoStmt* do_stmt) {
    /**
     * `do body while (cond);`
     * is transformed into
     * do {
     *   body [stripped of surrounding braces]
     *   if (!(cond)) { break; }
     * } while(1);
     */
    if (!exprHasFuncCall(do_stmt->getCond())) return true;

    std::string cond;
    llvm::raw_string_ostream cond_out(cond);
    do_stmt->getCond()->printPretty(cond_out, nullptr, m_policy);
    cond_out.flush();

    m_rewriter.ReplaceText(do_stmt->getCond()->getSourceRange(), "1");

    std::string cond_wrapped_in_if;
    llvm::raw_string_ostream cond_wrapped_out(cond_wrapped_in_if);
    cond_wrapped_out << "if (!(" << cond << ")) {\nbreak;\n}\n";
    cond_wrapped_out.flush();

    m_rewriter.InsertTextBefore(do_stmt->getBody()->getSourceRange().getEnd(), cond_wrapped_in_if);

    return true;
}

bool LoopToIRVisitor::exprHasFuncCall(clang::Expr* expr) {
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

// ─── CompoundStmtWrapperVisitor ──────────────────────────────────────────────

CompoundStmtWrapperVisitor::CompoundStmtWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool CompoundStmtWrapperVisitor::VisitForStmt(clang::ForStmt* for_stmt) {
    m_rewriter.InsertTextBefore(for_stmt->getSourceRange().getBegin(), "{\n");
    m_rewriter.InsertTextBefore(for_stmt->getSourceRange().getEnd().getLocWithOffset(1), "\n}");
    if(llvm::dyn_cast<clang::NullStmt>(for_stmt->getBody())) m_rewriter.ReplaceText(for_stmt->getBody()->getSourceRange(), "{}");
    return true;
}

bool CompoundStmtWrapperVisitor::VisitWhileStmt(clang::WhileStmt* while_stmt) {
    if(llvm::dyn_cast<clang::NullStmt>(while_stmt->getBody())) m_rewriter.ReplaceText(while_stmt->getBody()->getSourceRange(), "{}");
    return true;
}

bool CompoundStmtWrapperVisitor::VisitDoStmt(clang::DoStmt* do_stmt) {
    if(llvm::dyn_cast<clang::NullStmt>(do_stmt->getBody())) m_rewriter.ReplaceText(do_stmt->getBody()->getSourceRange(), "{}");
    return true;
}

bool CompoundStmtWrapperVisitor::VisitIfStmt(clang::IfStmt* if_stmt) {
    m_rewriter.InsertTextBefore(if_stmt->getSourceRange().getBegin(), "{\n");
    m_rewriter.InsertTextBefore(if_stmt->getSourceRange().getEnd().getLocWithOffset(1), "\n}");
    return true;
}

bool CompoundStmtWrapperVisitor::VisitSwitchStmt(clang::SwitchStmt* switch_stmt) {
    m_rewriter.InsertTextBefore(switch_stmt->getSourceRange().getBegin(), "{\n");
    m_rewriter.InsertTextBefore(switch_stmt->getSourceRange().getEnd().getLocWithOffset(1), "\n}");
    return true;
}

// ─── ParenExprWrapperVisitor ──────────────────────────────────────────────

ParenExprWrapperVisitor::ParenExprWrapperVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool ParenExprWrapperVisitor::VisitBinaryOperator(clang::BinaryOperator* bin_op) {
    if(bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Comma) {
        if (isWrappedWithParenExpr(bin_op)) return true;

        m_rewriter.InsertTextBefore(bin_op->getSourceRange().getBegin(), "(");
        m_rewriter.InsertTextAfterToken(bin_op->getSourceRange().getEnd(), ")");
    }
    return true;
}

bool ParenExprWrapperVisitor::isWrappedWithParenExpr(const clang::BinaryOperator* binary_op) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*binary_op);
    const auto& parent = m_ctx.getParents(current)[0];
    if(parent.get<clang::ParenExpr>() != nullptr) return true;
    else return false;
}

// ─── CommaInitVisitor ──────────────────────────────────────────────

CommaInitVisitor::CommaInitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

clang::SourceLocation CommaInitVisitor::findInsertionLoc(clang::DeclStmt* decl_stmt) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*decl_stmt);
    const clang::Stmt* last_stmt = decl_stmt;

    while (true) {
        auto parents = m_ctx.getParents(current);
        if (parents.empty())
            return {};

        const auto& parent = parents[0];
        if (parent.get<clang::CompoundStmt>())
            return last_stmt->getBeginLoc();

        if (const auto* stmt = parent.get<clang::Stmt>())
            last_stmt = stmt;

        current = parent;
    }
}

bool CommaInitVisitor::VisitDeclStmt(clang::DeclStmt* decl_stmt) {
    if (decl_stmt->isSingleDecl()) {
        return true;
    }

    llvm::SmallVector<const clang::VarDecl*, 8> vars;
    for (const clang::Decl* decl : decl_stmt->decls()) {
        if (const auto* var_decl = llvm::dyn_cast<clang::VarDecl>(decl)) {
            vars.push_back(var_decl);
        }
    }

    std::string replacement{};
    llvm::raw_string_ostream out(replacement);

    for (const clang::VarDecl* var_decl : vars) {
        var_decl->print(out, m_policy);
        out << ";\n";
    }
    out.flush();

    clang::SourceLocation nearest_compound_stmt = findInsertionLoc(decl_stmt);
    if (nearest_compound_stmt != decl_stmt->getSourceRange().getBegin()) {
        m_rewriter.InsertTextBefore(nearest_compound_stmt, replacement);
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), ";");
    } else {
        m_rewriter.ReplaceText(decl_stmt->getSourceRange(), replacement);
    }
    return true;
}

// ─── TemplateSpecializerVisitor ──────────────────────────────────────────────

TemplateSpecializerVisitor::TemplateSpecializerVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, clang::Sema& sema)
    : m_ctx(ctx), m_rewriter(rewriter), m_sema(sema) {
    
}

void TemplateSpecializerVisitor::fixDoubleAngleBrackets(std::string& str) {
    str.erase(str.find('<', str.find('<') + 1), 1);
    str.erase(str.rfind('>', str.find('{')), 1);
}

clang::SourceLocation TemplateSpecializerVisitor::getInsertionPoint(clang::FunctionTemplateDecl *FTD,
                                        clang::SourceManager &SM) {
  return FTD->getSourceRange().getEnd().getLocWithOffset(1);
}

std::string TemplateSpecializerVisitor::emitFunctionSpecialization(clang::FunctionDecl *Spec, bool isVariadicTemplate) {
  clang::PrintingPolicy PP(m_ctx.getLangOpts());
  // print the function definition
  PP.TerseOutput        = false;
  // print the fully qualified typenames used
  // inside the function definition
  PP.SuppressScope      = false;

  std::string Out;
  llvm::raw_string_ostream OS(Out);
  OS << "\n";
  Spec->print(OS, PP, 2, true);
  OS.flush();
  if (isVariadicTemplate) fixDoubleAngleBrackets(Out);
  return Out;
}

bool TemplateSpecializerVisitor::VisitFunctionTemplateDecl(clang::FunctionTemplateDecl *FTD) {
  if (!FTD->getTemplatedDecl()->isThisDeclarationADefinition())
    return true;
  std::string Insertions;
  for (clang::FunctionDecl *Spec : FTD->specializations()) {
    clang::TemplateSpecializationKind TSK = Spec->getTemplateSpecializationKind();

    // TSK_Undeclared                       - declaration for specialization but no definition
    // TSK_ImplicitInstantiation            - specialization through usage
    // TSK_ExplicitSpecialization           - specialization done explicitly
    // TSK_ExplicitInstantiationDeclaration - specialization declared explicitly but definition in another TU
    // TSK_ExplicitInstantiationDefinition  - specialization declared explicitly but definition in this TU
    if (TSK != clang::TSK_ImplicitInstantiation &&
        TSK != clang::TSK_ExplicitInstantiationDefinition)
      continue;

    if (!Spec->hasBody())
      continue;

    Insertions += emitFunctionSpecialization(Spec, FTD->getTemplateParameters()->hasParameterPack());
  }

  if (Insertions.empty())
    return true;

  clang::SourceLocation InsertLoc = getInsertionPoint(FTD, m_ctx.getSourceManager());
  if (InsertLoc.isInvalid()) {
    llvm::errs() << "Could not determine insertion point for "
                 << FTD->getQualifiedNameAsString() << "\n";
    return true;
  }

  m_rewriter.InsertTextAfter(InsertLoc, Insertions);
  return true;
}

clang::SourceLocation TemplateSpecializerVisitor::getInsertionPoint(clang::ClassTemplateDecl *CTD, clang::SourceManager &SM) {
  return CTD->getSourceRange().getEnd().getLocWithOffset(2);
}

std::string TemplateSpecializerVisitor::emitClassSpecialization(clang::ClassTemplateSpecializationDecl *Spec, bool isVariadicTemplate) {
  clang::PrintingPolicy PP(m_ctx.getLangOpts());
  PP.TerseOutput        = false;
  PP.SuppressScope      = false;

  std::string Out;
  llvm::raw_string_ostream OS(Out);
  OS << "\n";
  Spec->print(OS, PP, 2, true);
  OS << ";";
  OS.flush();
  if(isVariadicTemplate) fixDoubleAngleBrackets(Out);
  return Out;
}

void TemplateSpecializerVisitor::instantiate_class_vars(clang::ClassTemplateSpecializationDecl *Spec) {
    for (auto* D : Spec->decls()) {
        if (auto* VD = llvm::dyn_cast<clang::VarDecl>(D)) {
            m_sema.InstantiateVariableDefinition(VD->getLocation(), VD);
        }
    }
}

bool TemplateSpecializerVisitor::VisitClassTemplateDecl(clang::ClassTemplateDecl *CTD) {
  if (!CTD->getTemplatedDecl()->isThisDeclarationADefinition())
    return true;
  std::string Insertions;
  for (clang::ClassTemplateSpecializationDecl *Spec : CTD->specializations()) {
    clang::TemplateSpecializationKind TSK = Spec->getSpecializationKind();

    if (TSK != clang::TSK_ImplicitInstantiation &&
        TSK != clang::TSK_ExplicitInstantiationDefinition)
      continue;

    if (!Spec->isCompleteDefinition())
      continue;

    instantiate_class_vars(Spec);
    
    Insertions += emitClassSpecialization(Spec, CTD->getTemplateParameters()->hasParameterPack());
  }

  if (Insertions.empty())
    return true;

  clang::SourceLocation InsertLoc = getInsertionPoint(CTD, m_ctx.getSourceManager());
  if (InsertLoc.isInvalid()) {
    llvm::errs() << "Could not determine insertion point for "
                 << CTD->getQualifiedNameAsString() << "\n";
    return true;
  }

  m_rewriter.InsertTextAfter(InsertLoc, Insertions);
  return true;
}

// ─── FuncCallCleanupVisitor ────────────────────────────────────────

FuncCallCleanupVisitor::FuncCallCleanupVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, size_t& current_index)
    : m_ctx(ctx), m_rewriter(rewriter), m_is_uninstantiated_template(false), m_current_index(current_index), m_policy(m_ctx.getLangOpts()) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool FuncCallCleanupVisitor::did_cleanup() {
    return m_last_update_loc.getRawEncoding() != 0;
}

bool FuncCallCleanupVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if(func->isDependentContext()) m_is_uninstantiated_template = true;
    else m_is_uninstantiated_template = false;

    return true;
}

bool FuncCallCleanupVisitor::VisitCallExpr(clang::CallExpr* call) {
    // There can be cases where the template has not been fully resolved and thus the 
    // expressions which uses operators to zero in on the result are represented as a 
    // function call. To safeguard against them, we skip the pass if the call has not 
    // been fully resolved. 
    if(m_is_uninstantiated_template) return true;

    if (isTopLevelCall(call)) return true;

    hoist_call_at_nearest_compound_statement(call);

    return true;
}

void FuncCallCleanupVisitor::hoist_call_at_nearest_compound_statement(clang::CallExpr* call) {
    clang::SourceLocation nearest_compound_stmt = findInsertionLoc(call);

    // The function is probably a nested function present as a template guard check. 
    // We should skip such functions as they will be evaluated at compile time and have
    // no business being inlined in the runtime code. 
    if(nearest_compound_stmt.getRawEncoding() == 0)
        return;
    // If there is an overlap between the function call that we just hoisted and the 
    // function call that we intend to hoist
    if(nearest_compound_stmt == m_last_update_loc)
        return;

    // A function that does not return anything and is being hoisted just before its call should not
    // be hoisted as they will result in no noticeable change in the rewrite and cause a infinite loop.
    // for eg. a class method returning void. 
    if((nearest_compound_stmt == call->getSourceRange().getBegin()) && (call->getType()->isVoidType()))
        return;

    // A function that occurs as a param in another function call should not be hoisted 
    // as it would mess up the markers that rewriter uses to process the changes. Although
    // this case should be caught early on for most of the cases since they would share the
    // same nearest_compound_stmt loc but there are cases where this may not be entirely true. 
    // for instance, Expression Statements. 
    if((call->getSourceRange().getBegin() >= m_last_call_loc.getBegin()) && (call->getSourceRange().getEnd() <= m_last_call_loc.getEnd()))
       return;

    auto& src_mgr = m_ctx.getSourceManager();
    const auto& lang_opts = m_ctx.getLangOpts();

    auto call_text =
        clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(call->getSourceRange()), src_mgr, lang_opts)
            .str();

    std::string ret_var{};
    std::string clean_call{};
    llvm::raw_string_ostream out(clean_call);

    if (!call->getType()->isVoidType()) {
        clang::QualType retTy = call->getType();
        ret_var = "__func_hoist_ret__" + std::to_string(m_current_index++);
        retTy.print(out, m_policy, ret_var);
        out << " = ";
    } else {
        ret_var = "(void)0";
    }
    out << call_text << ";\n";
    out.flush();

    if(nearest_compound_stmt == call->getSourceRange().getBegin()) {
        // This case is called when we have functions being called on the 
        // result of the previous function
        // ie. a().b().c()
        m_rewriter.ReplaceText(call->getSourceRange(), clean_call + ret_var);
    } else {
        m_rewriter.InsertTextBefore(nearest_compound_stmt, clean_call);
        m_rewriter.ReplaceText(call->getSourceRange(), ret_var);
    }
    m_last_update_loc = nearest_compound_stmt;
    m_last_call_loc = call->getSourceRange();
}

clang::SourceLocation FuncCallCleanupVisitor::findInsertionLoc(const clang::CallExpr* call) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*call);
    const clang::Stmt* last_stmt = call;

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

bool FuncCallCleanupVisitor::isTopLevelCall(const clang::CallExpr* call) {
    // Guard: do not hoist calls that execute repeatedly —
    // i.e. calls in a loop condition or for-increment.
    // Handles: while(foo()), while(a = foo()),
    //          for(; foo(); ), for(; bar(), foo(); ), for(;; foo())
    {
        const clang::Stmt* child = call;
        while (true) {
            auto parents = m_ctx.getParents(*child);
            if (parents.empty()) break;
            const auto* parent = parents[0].get<clang::Stmt>();
            if (!parent) break;
            if (llvm::isa<clang::CompoundStmt>(parent)) break;

            if (const auto* fs = llvm::dyn_cast<clang::ForStmt>(parent)) {
                if (fs->getCond() == child || fs->getInc() == child)
                    return true;
                break;
            }
            if (const auto* ws = llvm::dyn_cast<clang::WhileStmt>(parent)) {
                if (ws->getCond() == child)
                    return true;
                break;
            }
            if (const auto* ds = llvm::dyn_cast<clang::DoStmt>(parent)) {
                if (ds->getCond() == child)
                    return true;
                break;
            }
            child = parent;
        }
    }

    auto parents = m_ctx.getParents(*call);
    if (parents.empty()) {
        return false;
    }

    const auto& parent_node = parents[0];

    // int x = foo();
    if (const auto* var_decl = parent_node.get<clang::VarDecl>()) {
        auto vd_parents = m_ctx.getParents(*var_decl);
        if (!vd_parents.empty()) {
            if (const auto* decl_stmt = vd_parents[0].get<clang::DeclStmt>()) {
                auto ds_parents = m_ctx.getParents(*decl_stmt);
                if (!ds_parents.empty()) {
                    if (const auto* parent_stmt = ds_parents[0].get<clang::Stmt>()) {
                        if (llvm::isa<clang::CompoundStmt>(parent_stmt)) {
                            return true;
                        }
                    }
                }
            }
        }
    } else if (const auto* parent_stmt = parent_node.get<clang::Stmt>()) {
        // x = foo();
        // note that x += foo() would need to be hoisted because x can be a pointer
        // and the way we inline right now means that the pointer update can get awry
        // if we take a pointer to the pointer with the type of the return value of the
        // the function and then dereference it to update. 
        if (const auto* bin_op = llvm::dyn_cast<clang::BinaryOperator>(parent_stmt)) {
            if (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Assign) {
                auto bo_parents = m_ctx.getParents(*bin_op);
                if (!bo_parents.empty()) {
                    if (const auto* grandparent = bo_parents[0].get<clang::Stmt>()) {
                        if (llvm::isa<clang::CompoundStmt>(grandparent)) {
                            return true;
                        }
                    }
                }
            }
            // foo();
        } else if (llvm::isa<clang::CompoundStmt>(parent_stmt)) {
            // we still hoist function calls that have a valid return value. 
            // This is because the function call can occur as the last expr
            // in a Expression Statement. 
            // int res = ({
            //    foo(); // returns an int
            // })
            if (call->getType()->isVoidType())
                return true;
        }
    }
    return false;
}

// ─── FilterVisitor ─────────────────────────────────────────────────

FilterVisitor::FilterVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter) : m_ctx(ctx), m_rewriter(rewriter) {
    
}

bool FilterVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody())
        return true;

    std::string func_str;
    llvm::raw_string_ostream stream(func_str);

    clang::PrintingPolicy policy = m_ctx.getPrintingPolicy();
    policy.Indentation = 2;

    func->print(stream, policy);
    stream.flush();

    std::string name = func->getNameInfo().getName().getAsString();
    m_functions[name] = stream.str();
    return true;
}

std::string FilterVisitor::toJSON() {
    llvm::json::Object root;
    for (const auto& [name, body] : m_functions)
        root[name] = body;

    std::string result;
    llvm::raw_string_ostream stream(result);
    stream << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(root)));
    stream.flush();
    return result;
}

// ─── FunctionParamRenamer ───────────────────────────────────────────

FunctionParamRenamer::FunctionParamRenamer(clang::ASTContext &C, std::map<int, std::string> &PR)
    : Ctx(C), callerParams(PR) {}

bool FunctionParamRenamer::VisitDeclRefExpr(clang::DeclRefExpr *DRE) {
    if (clang::VarDecl *VD = llvm::dyn_cast<clang::VarDecl>(DRE->getDecl()))
        if (clang::ParmVarDecl *PVD = llvm::dyn_cast<clang::ParmVarDecl>(VD))
            VD->setDeclName(clang::DeclarationName(&Ctx.Idents.get(callerParams[PVD->getFunctionScopeIndex()])));
    return true;
}

// ─── InlinePrinterHelper ───────────────────────────────────────────

InlinePrinterHelper::InlinePrinterHelper(clang::ASTContext& ctx, std::string target_var,
                                         std::map<int, std::string> caller_params, clang::PrintingPolicy policy,
                                         size_t current_index)
    : m_ctx(ctx),
      m_target_var(std::move(target_var)),
      m_caller_params(std::move(caller_params)),
      m_policy(policy), 
      m_current_index(current_index), 
      m_goto_labels({}),
      m_func_param_renamer(m_ctx, m_caller_params),
      m_goto_label_index(0) {
        if (m_ctx.getLangOpts().CPlusPlus)
            m_policy.adjustForCPlusPlus();
        m_policy.IncludeTagDefinition = true;
        m_policy.SuppressTagKeyword = false;
      }

void InlinePrinterHelper::renameFuncParamInType(clang::QualType T) {
    const clang::Type *TP = T.getTypePtr();

    if (const auto *TOE = llvm::dyn_cast<clang::TypeOfExprType>(TP)) {
        if (clang::Expr *e = TOE->getUnderlyingExpr()) {
            m_func_param_renamer.TraverseStmt(e);
        }
        renameFuncParamInType(TOE->desugar());
        return;
    } else if (const auto *TOT = llvm::dyn_cast<clang::TypeOfType>(TP)) {
        renameFuncParamInType(TOT->desugar());
        return;
    }

    if (const auto *VAT = llvm::dyn_cast<clang::VariableArrayType>(TP)) {
        if (clang::Expr *size = VAT->getSizeExpr())
            m_func_param_renamer.TraverseStmt(size);
        renameFuncParamInType(VAT->getElementType());
    } else if (const auto *PT = llvm::dyn_cast<clang::PointerType>(TP)) {
        renameFuncParamInType(PT->getPointeeType());
    } else if (const auto *RT = llvm::dyn_cast<clang::ReferenceType>(TP)) {
        renameFuncParamInType(RT->getPointeeType());
    } else if (const auto *AT = llvm::dyn_cast<clang::ArrayType>(TP)) {
        renameFuncParamInType(AT->getElementType());
    } else if (const auto *FPT = llvm::dyn_cast<clang::FunctionProtoType>(TP)) {
        renameFuncParamInType(FPT->getReturnType());
        for (auto paramType : FPT->param_types())
            renameFuncParamInType(paramType);
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        renameFuncParamInType(FT->getReturnType());
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        renameFuncParamInType(FT->getReturnType());
    } else if (const auto *MPT = llvm::dyn_cast<clang::MemberPointerType>(TP)) {
        renameFuncParamInType(MPT->getPointeeType());
    } else if (const auto *VT = llvm::dyn_cast<clang::VectorType>(TP)) {
        renameFuncParamInType(VT->getElementType());
    } else if (const auto *ParenT = llvm::dyn_cast<clang::ParenType>(TP)) {
        renameFuncParamInType(ParenT->getInnerType());
    }
}

bool InlinePrinterHelper::handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) {
    if (auto* decl_ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt)) {
        m_func_param_renamer.TraverseStmt(decl_ref);
        return false;
    }

    if (auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(stmt)) {
        for (auto it = decl_stmt->decl_begin(); it != decl_stmt->decl_end(); ++it) {
            if (auto* var_decl = llvm::dyn_cast<clang::VarDecl>(*it)) {
                // renames function parameters used in types, for eg. :
                // T arr[n] -> T arr[__inline_x_y]
                // typeof(T[n]) -> typeof(T[__inline_x_y])
                // typeof(... n ...) -> typeof(... __inline_x_y ...)
                renameFuncParamInType(var_decl->getType());

                // T x = ... n ... -> T x = ... __inline_x_y ...
                if (clang::Expr *init = var_decl->getInit())
                    m_func_param_renamer.TraverseStmt(init);
            }
        }
        // we let the printer figure out how to print the declarations
        // as there can be comma separated decls which we can't handle ourselves. 
        // One solution is to separate them into different lines but this would not
        // work when these decls are present in a for loop. The VLA type need to be
        // renamed separately because it is not a DeclRefExpr, rather its a part of the
        // type itself. Here we take a slightly controversial approach of modifying the AST
        // itself instead of doing the tweaks using the rewriter. The perils of such is 
        // usually descried as accidently changing a node which might be shared by some
        // other subtree. But for our case, the type of a VLA need to be renamed if it
        // uses a function param, hence even if its shared, its okay because the latter
        // would anyways need to be renamed to reflect the inlined function params. 
        return false;
    }

    if (auto* for_stmt = llvm::dyn_cast<clang::ForStmt>(stmt)) {
        if (auto* init = for_stmt->getInit())
            m_func_param_renamer.TraverseStmt(init);
        return false;
    }

    if (auto* if_stmt = llvm::dyn_cast<clang::IfStmt>(stmt)) {
        if (auto* init = if_stmt->getInit())
            m_func_param_renamer.TraverseStmt(init);
        return false;
    }

    if (auto* switch_stmt = llvm::dyn_cast<clang::SwitchStmt>(stmt)) {
        if (auto* init = switch_stmt->getInit())
            m_func_param_renamer.TraverseStmt(init);
        return false;
    }

    if (auto* gcc_asm = llvm::dyn_cast<clang::GCCAsmStmt>(stmt)) {
        m_func_param_renamer.TraverseStmt(gcc_asm);

        std::string asm_stmt;
        llvm::raw_string_ostream asm_out(asm_stmt);
        gcc_asm->printPretty(asm_out, nullptr, m_policy);
        asm_out.flush();

        asm_stmt.replace(0, 3, "__asm__");
        stream << asm_stmt << ";";
        return true;
    }

    if (auto* return_stmt = llvm::dyn_cast<clang::ReturnStmt>(stmt)) {
        if (!m_target_var.empty())
            stream << m_target_var << " = ";
        if (clang::Expr* ret_val = return_stmt->getRetValue())
            ret_val->printPretty(stream, this, m_policy);
        stream << ";\n";
        return true;
    }

    return false;
}

// ─── LabelMaker ───────────────────────────
class LabelMaker : public clang::RecursiveASTVisitor<LabelMaker> {
private:
    clang::ASTContext& m_ctx;
    size_t m_current_index;
    std::map<std::string, std::string> m_goto_labels;
    size_t m_goto_label_index;
public:
    LabelMaker(clang::ASTContext& ctx, size_t current_index) : m_ctx(ctx),
                                                               m_current_index(current_index), 
                                                               m_goto_labels({}),
                                                               m_goto_label_index(0) { }

    bool VisitLabelStmt(clang::LabelStmt* label_stmt) {
        clang::LabelDecl *decl = label_stmt->getDecl();
        clang::IdentifierInfo &newII = m_ctx.Idents.get(std::string("__inline_label_") + std::to_string(m_current_index) + "__" + std::to_string(m_goto_label_index++));
        decl->setDeclName(clang::DeclarationName(&newII));

        return true;
    }
};

// ─── InlineVisitor - FunctionCallCleanup ───────────────────────────

bool InlineVisitor::callInLoop(const clang::CallExpr* call) {
    // Guard: do not hoist calls that execute repeatedly —
    // i.e. calls in a loop condition or for-increment.
    // Handles: while(foo()), while(a = foo()),
    //          for(; foo(); ), for(; bar(), foo(); ), for(;; foo())
    {
        const clang::Stmt* child = call;
        while (true) {
            auto parents = m_ctx.getParents(*child);
            if (parents.empty()) break;
            const auto* parent = parents[0].get<clang::Stmt>();
            if (!parent) break;
            if (llvm::isa<clang::CompoundStmt>(parent)) break;

            if (const auto* fs = llvm::dyn_cast<clang::ForStmt>(parent)) {
                if (fs->getCond() == child || fs->getInc() == child)
                    return true;
                break;
            }
            if (const auto* ws = llvm::dyn_cast<clang::WhileStmt>(parent)) {
                if (ws->getCond() == child)
                    return true;
                break;
            }
            if (const auto* ds = llvm::dyn_cast<clang::DoStmt>(parent)) {
                if (ds->getCond() == child)
                    return true;
                break;
            }
            child = parent;
        }
    }
    return false;
}

bool InlineVisitor::isTopLevelCall(const clang::CallExpr* call) {
    auto parents = m_ctx.getParents(*call);
    if (parents.empty()) {
        return false;
    }

    const auto& parent_node = parents[0];

    // int x = foo();
    if (const auto* var_decl = parent_node.get<clang::VarDecl>()) {
        auto vd_parents = m_ctx.getParents(*var_decl);
        if (!vd_parents.empty()) {
            if (const auto* decl_stmt = vd_parents[0].get<clang::DeclStmt>()) {
                auto ds_parents = m_ctx.getParents(*decl_stmt);
                if (!ds_parents.empty()) {
                    if (const auto* parent_stmt = ds_parents[0].get<clang::Stmt>()) {
                        if (llvm::isa<clang::CompoundStmt>(parent_stmt)) {
                            return true;
                        }
                    }
                }
            }
        }
    } else if (const auto* parent_stmt = parent_node.get<clang::Stmt>()) {
        // x = foo();
        // note that x += foo() would need to be hoisted because x can be a pointer
        // and the way we inline right now means that the pointer update can get awry
        // if we take a pointer to the pointer with the type of the return value of the
        // the function and then dereference it to update. 
        if (const auto* bin_op = llvm::dyn_cast<clang::BinaryOperator>(parent_stmt)) {
            if (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Assign) {
                auto bo_parents = m_ctx.getParents(*bin_op);
                if (!bo_parents.empty()) {
                    if (const auto* grandparent = bo_parents[0].get<clang::Stmt>()) {
                        if (llvm::isa<clang::CompoundStmt>(grandparent)) {
                            return true;
                        }
                    }
                }
            }
            // foo();
        } else if (llvm::isa<clang::CompoundStmt>(parent_stmt)) {
            return true;
        }
    }
    return false;
}

// ─── InlineVisitor ─────────────────────────────────────────────────

InlineVisitor::InlineVisitor(clang::ASTContext& ctx, const std::string& caller, const std::string& callee,
                             std::vector<size_t>& call_sites, size_t& current_index,
                             clang::Rewriter& rewriter, std::map<std::string, clang::FunctionDecl*>& decl_map)
    : m_ctx(ctx),
      m_caller(caller),
      m_callee(callee),
      m_call_sites(call_sites),
      m_current_index(current_index),
      m_rewriter(rewriter),
      m_policy(m_ctx.getLangOpts()),
      m_function_decl_map(decl_map) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool InlineVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    m_current_func = func->getNameInfo().getName().getAsString();
    return true;
}

bool InlineVisitor::shouldVisitImplicitCode() const { return true; }

bool InlineVisitor::VisitCXXConstructExpr(clang::CXXConstructExpr* expr) {
    if (m_matched) return true;

    if (m_caller != m_current_func)
        return true;

    clang::CXXConstructorDecl* ctor = expr->getConstructor();
    if (!ctor)
        return true;

    std::string callee_name = ctor->getParent()->getNameAsString();
    if (m_callee != callee_name)
        return true;


    auto& src_mgr = m_ctx.getSourceManager();
    auto ploc = src_mgr.getPresumedLoc(src_mgr.getSpellingLoc(expr->getExprLoc()));

    if (ploc.getLine() != m_call_sites[m_current_index])
        return true;

    // Mark matched so the caller knows this site was found,
    // but skip inlining for all special member functions.
    m_matched = true;
    return true;
}

bool InlineVisitor::VisitCallExpr(clang::CallExpr* call) {
    if (m_matched)
        return true;

    if (m_caller != m_current_func)
        return true;

    auto& src_mgr = m_ctx.getSourceManager();
    auto loc = src_mgr.getSpellingLoc(call->getExprLoc());
    auto ploc = src_mgr.getPresumedLoc(loc);

    if (ploc.getLine() != m_call_sites[m_current_index])
        return true;

    // If the call is in a loop that we do not correctly
    // handle inlining or the functionHositingPass failed
    // in which case we do not inline
    if (callInLoop(call) || !isTopLevelCall(call))
        return true;

    clang::FunctionDecl* callee = call->getDirectCallee();
    // the callee might be a function pointer. In the AST for such callsites,
    // the callee is not available so we query a map that stores the function_name
    // -> function_decl information.
    if (!callee)
        callee = m_function_decl_map[m_callee];

    if(!callee->hasBody())
        return true;

    std::string callee_name;
    if (callee) {
        callee_name = callee->getNameAsString();
    } else {
        auto* callee_expr = call->getCallee()->IgnoreParenImpCasts();
        if (auto* member = llvm::dyn_cast<clang::MemberExpr>(callee_expr))
            // obj.foo() or obj->foo() — resolved member call
            callee_name = member->getMemberNameInfo().getAsString();

        else if (auto* dre = llvm::dyn_cast<clang::DeclRefExpr>(callee_expr))
            // foo() — resolved free function
            callee_name = dre->getNameInfo().getAsString();

        else if (auto* unresolved = llvm::dyn_cast<clang::UnresolvedLookupExpr>(callee_expr))
            // foo<N>() — unresolved free function in uninstantiated template
            callee_name = unresolved->getName().getAsString();

        else if (auto* unresolved_member = llvm::dyn_cast<clang::UnresolvedMemberExpr>(callee_expr))
            // obj.foo<N>() — unresolved member call in uninstantiated template
            callee_name = unresolved_member->getMemberName().getAsString();

        else if (auto* dep_member = llvm::dyn_cast<clang::CXXDependentScopeMemberExpr>(callee_expr))
            // obj->foo() where obj's type is dependent
            callee_name = dep_member->getMember().getAsString();

        else if (auto* dep_ref = llvm::dyn_cast<clang::DependentScopeDeclRefExpr>(callee_expr))
            // T::foo() where T is a dependent type
            callee_name = dep_ref->getDeclName().getAsString();
        else if(auto* c_cast_ref = llvm::dyn_cast<clang::CStyleCastExpr>(callee_expr))
            // typedef T(*ptr)(U, V)
            // ((ptr)func1->ptr)(a, b)
            callee_name = "";
        else if (auto* ha_ref = llvm::dyn_cast<clang::ArraySubscriptExpr>(callee_expr))
            callee_name = "";
        else {
            llvm::errs() << "Unhandled callee type: " << callee_expr->getStmtClassName() << " at: " << m_call_sites[m_current_index] << " Caller = " << m_caller << " Callee = " << m_callee << "\n";
            callee_name = "";
        }
    }

    // This check is required in case the function hoisting pass failed. Because then we can
    // have foo(bar()) and we can match with foo instead.
    if (m_callee != callee_name)
        return true;

    // This check safeguards against a segfault further down the line. Once the preprocessing
    // is good enough, we remove this check though.
    if(call->getNumArgs() != callee->getNumParams())
        return true;

    // TODO: Handle class methods — they may use private members
    // that need special treatment.
    if (llvm::isa<clang::CXXMethodDecl>(callee))
        return true;
    
    m_matched = true;

    std::string target_var{};
    auto parents = m_ctx.getParents(*call);
    const clang::Stmt* stmt_to_replace = call;

    std::string inlined_code{};
    llvm::raw_string_ostream OS(inlined_code);
    size_t added_lines = 0;

    if (!parents.empty()) {
        // xvalue cleanup
        if (const auto* expr_cleanup = parents[0].get<clang::ExprWithCleanups>())
            parents = m_ctx.getParents(*expr_cleanup);

        const auto& parent_node = parents[0];

        // int x = foo();
        // we remove constness from the type as we first declare the variable. 
        // This is necessary because the callee can have multiple branches with return
        // statements
        if (const auto* var_decl = parent_node.get<clang::VarDecl>()) {
            clang::VarDecl *NewVD = clang::VarDecl::Create(
                m_ctx,
                const_cast<clang::DeclContext*>(var_decl->getDeclContext()),
                var_decl->getBeginLoc(),
                var_decl->getLocation(),
                var_decl->getIdentifier(),
                var_decl->getType(),
                m_ctx.getTrivialTypeSourceInfo(var_decl->getType().getUnqualifiedType()),
                var_decl->getStorageClass()
            );
            NewVD->setConstexpr(var_decl->isConstexpr());
            NewVD->setTSCSpec(var_decl->getTSCSpec());
            if(var_decl->isInlineSpecified())
                NewVD->setInlineSpecified();

            for (clang::Attr *A : var_decl->attrs())
                NewVD->addAttr(A->clone(m_ctx));
            NewVD->print(OS, m_policy);
            OS << ";\n";
            added_lines += 1;
            target_var = var_decl->getNameAsString();

            auto grandparents = m_ctx.getParents(*var_decl);
            if (!grandparents.empty()) {
                if (const auto* grandparent_decl_stmt = grandparents[0].get<clang::DeclStmt>())
                    stmt_to_replace = grandparent_decl_stmt;
            }
        }
        // x = foo();
        // note that x += foo() would need to be hoisted because x can be a pointer
        // and the way we inline right now means that the pointer update can get awry
        // if we take a pointer to the pointer with the type of the return value of the
        // the function and then dereference it to update. 
        else if (const auto* parent_stmt = parent_node.get<clang::Stmt>()) {
            if (const auto* bin_op = llvm::dyn_cast<clang::BinaryOperator>(parent_stmt)) {
                if (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Assign) {
                    target_var =
                        clang::Lexer::getSourceText(
                            clang::CharSourceRange::getTokenRange(bin_op->getLHS()->getSourceRange()), src_mgr,
                            m_ctx.getLangOpts())
                            .str();
                    stmt_to_replace = bin_op;
                }
            }
        }
    }

    OS << "{";

    std::map<int, std::string> caller_params;
    for (unsigned i = 0; i < call->getNumArgs(); ++i) {
        clang::ParmVarDecl* param_decl = callee->getParamDecl(i);
        clang::Expr* arg = call->getArg(i);

        std::string arg_text;
        if (auto* default_arg = llvm::dyn_cast<clang::CXXDefaultArgExpr>(arg)) {
            llvm::raw_string_ostream stream(arg_text);
            default_arg->getExpr()->printPretty(stream, nullptr, m_policy);
            stream.flush();
        } else {
            llvm::raw_string_ostream stream(arg_text);
            arg->printPretty(stream, nullptr, m_policy);
            stream.flush();
        }

        // if we are inlining a function that takes in variable number of arguments, we let it pass by reference
        // in most real world scenarios, we are mostly just going to read values from it. 
        if (param_decl->getType()->isLValueReferenceType() || param_decl->getOriginalType().getAsString() == "va_list") {
            caller_params[i] = arg_text;
        } else {
            caller_params[i] = "__inline_param__" + std::to_string(m_current_index) + "_" + std::to_string(i);
            param_decl->getType().print(OS, m_policy, caller_params[i]);
            OS << " = " << arg_text << ";\n";
            added_lines++;
        }
    }

    std::string raw_body;
    llvm::raw_string_ostream stream(raw_body);

    LabelMaker visitor(m_ctx, m_current_index);
    visitor.TraverseStmt(callee->getBody());

    InlinePrinterHelper helper(m_ctx, target_var, caller_params, m_policy, m_current_index);
    callee->getBody()->printPretty(stream, &helper, m_policy);
    stream.flush();

    // Strip outer braces.
    size_t first_brace = raw_body.find('{');
    size_t last_brace = raw_body.rfind('}');
    if (first_brace != std::string::npos && last_brace != std::string::npos && first_brace < last_brace)
        raw_body = raw_body.substr(first_brace + 1, last_brace - first_brace - 1);

    // Trim leading/trailing blank lines.
    size_t first_content = raw_body.find_first_not_of(" \n\r\t");
    size_t last_content = raw_body.find_last_not_of(" \n\r\t");
    if (first_content != std::string::npos && last_content != std::string::npos)
        raw_body = raw_body.substr(first_content, last_content - first_content + 1);

    added_lines += std::count(raw_body.begin(), raw_body.end(), '\n');
    OS << raw_body;

    // ReplaceText swaps the original statement for inlined_code.
    // added_lines counts newlines in the replacement text, but the
    // original statement may itself span multiple source lines (e.g.
    // a call whose arguments wrap).  The true shift for everything
    // below this point is the difference between the two:
    //
    //   net = (newlines in replacement) - (newlines in original)
    //
    // Using added_lines directly would over-count by one for every
    // extra line the original statement occupied, causing cumulative
    // drift in later call-site lookups.
    auto orig_text = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(stmt_to_replace->getSourceRange()), src_mgr, m_ctx.getLangOpts());
    m_net_lines = added_lines - std::count(orig_text.begin(), orig_text.end(), '\n');

    OS << "}";
    OS.flush();
    m_rewriter.ReplaceText(stmt_to_replace->getSourceRange(), inlined_code);
    return true;
}

bool InlineVisitor::matched() const { return m_matched; }

size_t InlineVisitor::get_net_change() { return m_net_lines; }

// ─── FunctionDeclMapVisitor ──────────────────────────────────────────────

FunctionDeclMapVisitor::FunctionDeclMapVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool FunctionDeclMapVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody()) return true;
    if (!func->isThisDeclarationADefinition()) return true;

    m_function_decl_map[func->getNameAsString()] = func;
    return true;
}

std::map<std::string, clang::FunctionDecl*>& FunctionDeclMapVisitor::get_function_decl_map()  {
    return m_function_decl_map;
}

// ─── ConstantFoldVisitor ──────────────────────────────────────────────

constantFoldVisitor::constantFoldVisitor(clang::ASTContext& context, clang::Rewriter& rewriter) : m_ctx(context), m_rewriter(rewriter) { }

bool constantFoldVisitor::did_fold() {
    return (m_last_update_loc_begin.getRawEncoding() != 0) && (m_last_update_loc_end.getRawEncoding() != 0);
}

bool constantFoldVisitor::is_parent_typeof(clang::Expr* expr) {
    const clang::Expr* current = expr;

    while (true) {
        auto parents = m_ctx.getParents(*current);
        if (parents.empty()) return false;

        const auto* parent_stmt = parents[0].get<clang::Type>();
        if (!parent_stmt) return false;

        if (llvm::dyn_cast<clang::TypeOfExprType>(parent_stmt)) {
            return true;
        }

        return false;
    }
}

bool constantFoldVisitor::is_constant_literal(clang::Expr* inner) {
    if (is_parent_typeof(inner)) inner = inner->IgnoreParens();
    if (llvm::isa<clang::IntegerLiteral>(inner)         ||
        llvm::isa<clang::FloatingLiteral>(inner)        ||
        llvm::isa<clang::CharacterLiteral>(inner)       ||
        llvm::isa<clang::StringLiteral>(inner)          ||
        llvm::isa<clang::CXXBoolLiteralExpr>(inner)     ||
        llvm::isa<clang::CXXNullPtrLiteralExpr>(inner)  ||
        llvm::isa<clang::ImaginaryLiteral>(inner)       ||
        llvm::isa<clang::FixedPointLiteral>(inner)      ||
        llvm::isa<clang::UserDefinedLiteral>(inner)     ||
        llvm::isa<clang::GNUNullExpr>(inner)            ||
        llvm::isa<clang::CompoundLiteralExpr>(inner)    ||
        llvm::isa<clang::InitListExpr>(inner))         
        return true;
    return false;
}

bool constantFoldVisitor::VisitExpr(clang::Expr* expr) {
    if (expr->getSourceRange().isInvalid()) return true;
    if ((expr->getSourceRange().getBegin() >= m_last_update_loc_begin) && (expr->getSourceRange().getEnd() <= m_last_update_loc_end)) return true;

    // ── DEBUG ────────────────────────────────────────────────────────────────
    // llvm::errs() << "\n=== [constantFold] Checking AST of expression to fold ===\n";
    // auto& src_mgr = m_ctx.getSourceManager();
    // auto& lang_opts = m_ctx.getLangOpts();

    // auto expr_text = clang::Lexer::getSourceText(
    //     clang::CharSourceRange::getTokenRange(expr->getSourceRange()),
    //     src_mgr, lang_opts
    // ).str();

    // llvm::errs() << "Source text: " << expr_text << "\n";
    // ─────────────────────────────────────────────────────────────────────────

    clang::Expr* inner = expr->IgnoreImpCasts();
    if (is_constant_literal(inner)) return true;

    clang::QualType exprTy = expr->getType();

    // Guard against null/invalid types (e.g. uninstantiated templates)
    if (exprTy.isNull() || exprTy->isDependentType() || exprTy->isUndeducedType())
        return true;

    // Don't fold enums into their underlying type. Note that we don't care if
    // the underlying type is enum as we should use enum vals to do arith ops
    // if the final type of the expr is an arith type
    if (exprTy->isEnumeralType())
        return true;

    // -2 -> negop (numeric literal)
    // The Pass will loop indefinitely if it sees a negation literal followed by a
    // numeric expression. This is because C grammar parses it as a unop. It will then
    // emit it as a unop only. We match on this and exit early if that is the case. 
    // We don't need to worry about paranthesis and implicit casts as they will get removed
    // by themselves as the pass goes to deeper expressions.
    if (auto* unary = llvm::dyn_cast<clang::UnaryOperator>(expr->IgnoreParenImpCasts()))
        if (unary->getOpcode() == clang::UO_Minus)
            if (llvm::isa<clang::IntegerLiteral>(unary->getSubExpr()) ||
                llvm::isa<clang::FloatingLiteral>(unary->getSubExpr()))
                return true;

    // If this expr's address is being taken, folding would turn `&arr[i]` into
    // `&<constant>`, which is invalid (can't take the address of an rvalue literal
    auto parents = m_ctx.getParents(*expr);
    for (const auto& parent : parents) {
        if (const auto* unary = parent.get<clang::UnaryOperator>()) {
            if (unary->getOpcode() == clang::UO_AddrOf) {
                return true;
            }
        }
    }

    // Don't fold comma expressions. The operands other than the last one
    // are evaluated purely for side effects (e.g. (0, funcCall(), 4)).
    // Folding would silently discard those side effects.
    if (auto* bin_op = llvm::dyn_cast<clang::BinaryOperator>(expr->IgnoreParenImpCasts()))
        if (bin_op->getOpcode() == clang::BO_Comma)
            return true;

    // Don't fold builtin calls (e.g. __builtin_nanf, __builtin_inf, etc.)
    // as they resolve to something that is internal to the compiler and will thus
    // not be valid when sent into compilation by replacing it. For instance, 
    // __builtin_nanf ("") resolves to nan, which clang sees as a reference to the
    // function nan. 
    if (auto* call = llvm::dyn_cast<clang::CallExpr>(expr->IgnoreParenImpCasts()))
        if (auto* fn = llvm::dyn_cast_or_null<clang::FunctionDecl>(call->getCalleeDecl()))
            if (fn->getBuiltinID() != 0)
                return true;

    if (expr->isValueDependent() || inner->isValueDependent())
        return true;
    if (!expr->isEvaluatable(m_ctx, clang::Expr::SideEffectsKind::SE_NoSideEffects) || !inner->isEvaluatable(m_ctx, clang::Expr::SideEffectsKind::SE_NoSideEffects))
        return true;

    clang::Expr::EvalResult result;
    if(!expr->EvaluateAsRValue(result, m_ctx)) return true;

    clang::APValue value = result.Val;
    // The resultant value can be a complex type such as a struct or an array
    // if we are indexing into an array that is declared const static. While the compiler
    // can determine at compile time the values that would need to be accessed via these const
    // globals, the programmer can very well ask for their address. In that case, constant folding
    // would lead to an error. Hence we conservatively stop such ill-transformations. We also add a
    // check against lvalues because clang under the hood represents a const static array (arr)
    // as &arr[0]. Now that is fine if only while printing it wrapped it in paranthesis. It does not 
    // and that is erroneous. Consider arr[1]. This gets transformed to &arr[0][1]. Now arr is a 1D array
    // and we are trying to derefence it twice since according to C type inference rules, [] has more
    // precedence than &. 
    if (value.isStruct() || value.isArray() || value.isLValue())
        return true;

    std::string replacement{};
    llvm::raw_string_ostream OS(replacement);
    value.printPretty(OS, m_ctx, expr->getType());
    OS.flush();

    // ── DEBUG ────────────────────────────────────────────────────────────────
    // llvm::errs() << "\n=== [constantFold] AST of expression being folded ===\n";
    // expr->dump();                                          // full AST subtree
    // ─────────────────────────────────────────────────────────────────────────

    m_last_update_loc_begin = expr->getSourceRange().getBegin();
    m_last_update_loc_end = expr->getSourceRange().getEnd();
    m_rewriter.ReplaceText(expr->getSourceRange(), replacement);

    return true;
}

// ─── deadCodeElimVisitor ──────────────────────────────────────────────

// If while doing a structural change, it so happens that there is a label inside of it, 
// then its possible that a goto statement outside this body jumps to this label via a 
// goto statement and we should be conservative about that. 
class LabelVerifier : public clang::RecursiveASTVisitor<LabelVerifier> {
private:
    bool has_label = false;
public:
    bool VisitLabelStmt(clang::LabelStmt* label_stmt) {
        has_label = true;
        return true;
    }

    bool has_label_stmt() {
        return has_label;
    }

    void reset() {
        has_label = false;
    }
};

deadCodeElimVisitor::deadCodeElimVisitor(clang::ASTContext& context, clang::Rewriter& rewriter) : m_ctx(context), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool deadCodeElimVisitor::did_elim() {
    return (m_last_update_loc_begin.getRawEncoding() != 0) && (m_last_update_loc_end.getRawEncoding() != 0);
}

bool deadCodeElimVisitor::has_label(clang::Stmt* stmt) {
    if(!stmt) return false;
    static LabelVerifier label_verifier;
    label_verifier.reset();
    label_verifier.TraverseStmt(stmt);
    return label_verifier.has_label_stmt();
}

// TODO: Need to change the assignment operator into a comma separated list 
//       with the end result as the last element of the list. This goes for 
//       a couple other compound operators as well. Maybe we will need to shift
//       this to the preprocessor step. 
bool deadCodeElimVisitor::VisitIfStmt(clang::IfStmt* if_stmt) {
    if ((if_stmt->getSourceRange().getBegin() >= m_last_update_loc_begin) && (if_stmt->getSourceRange().getEnd() <= m_last_update_loc_end)) return true;

    clang::Expr* __cond = if_stmt->getCond();
    // allowSideEffects let statements like (a = 45, 45) pass which
    // are then caught later and added to the replacement string. 
    if(__cond->isValueDependent() || !(__cond->isEvaluatable(m_ctx, clang::Expr::SideEffectsKind::SE_AllowSideEffects)))
        return true;
    bool __cond_val;
    if (!__cond->EvaluateAsBooleanCondition(__cond_val, m_ctx)) return true;
    if(__cond_val) {
        if (clang::Stmt* else_stmt = if_stmt->getElse()) {
            if (has_label(else_stmt)) return true;
        }
    } else {
        if(has_label(if_stmt->getThen())) return true;
    }
    bool __cond_has_side_effects =  __cond->HasSideEffects(m_ctx, /* includePossibleSideEffects = */ true);

    std::string reduced{};
    llvm::raw_string_ostream reduced_stream(reduced);

    if (__cond_has_side_effects) {
        __cond->printPretty(reduced_stream, nullptr, m_policy);
        reduced_stream << ";";
    }

    if (__cond_val) {
        if_stmt->getThen()->printPretty(reduced_stream, nullptr, m_policy);
    } else {
        if (clang::Stmt* else_stmt = if_stmt->getElse()) {
            else_stmt->printPretty(reduced_stream, nullptr, m_policy);
        }
    }

    reduced_stream.flush();

    m_last_update_loc_begin = if_stmt->getSourceRange().getBegin();
    m_last_update_loc_end = if_stmt->getSourceRange().getEnd();
    m_rewriter.ReplaceText(if_stmt->getSourceRange(), reduced);

    return true;
}

bool deadCodeElimVisitor::VisitForStmt(clang::ForStmt* for_stmt) {
    if ((for_stmt->getSourceRange().getBegin() >= m_last_update_loc_begin) && (for_stmt->getSourceRange().getEnd() <= m_last_update_loc_end)) return true;

    clang::Expr* __cond = for_stmt->getCond();
    if (!__cond) return true;
    // allowSideEffects let statements like (a = 45, 45) pass which
    // are then caught later and added to the replacement string. 
    if(__cond->isValueDependent() || !(__cond->isEvaluatable(m_ctx, clang::Expr::SideEffectsKind::SE_AllowSideEffects)))
        return true;
    bool __cond_val;
    if (!__cond->EvaluateAsBooleanCondition(__cond_val, m_ctx)) return true;
    if (!__cond_val) {
        if (has_label(for_stmt->getBody())) return true;

        std::string reduced{};
        llvm::raw_string_ostream reduced_stream(reduced);
        for_stmt->getInit()->printPretty(reduced_stream, nullptr, m_policy);
        
        bool __cond_has_side_effects = __cond->HasSideEffects(m_ctx, /* includePossibleSideEffects = */ true);
        if (__cond_has_side_effects) {
            __cond->printPretty(reduced_stream, nullptr, m_policy);
            reduced_stream << ";";
        }

        reduced_stream.flush();

        m_last_update_loc_begin = for_stmt->getSourceRange().getBegin();
        m_last_update_loc_end = for_stmt->getSourceRange().getEnd();
        m_rewriter.ReplaceText(for_stmt->getSourceRange(), reduced);
    }

    return true;
}

bool deadCodeElimVisitor::VisitWhileStmt(clang::WhileStmt* while_stmt) {
    if ((while_stmt->getSourceRange().getBegin() >= m_last_update_loc_begin) && (while_stmt->getSourceRange().getEnd() <= m_last_update_loc_end)) return true;

    clang::Expr* __cond = while_stmt->getCond();
    // allowSideEffects let statements like (a = 45, 45) pass which
    // are then caught later and added to the replacement string. 
    if(__cond->isValueDependent() || !(__cond->isEvaluatable(m_ctx, clang::Expr::SideEffectsKind::SE_AllowSideEffects)))
        return true;
    bool __cond_val;
    if (!__cond->EvaluateAsBooleanCondition(__cond_val, m_ctx)) return true;
    if (!__cond_val) {
        if (has_label(while_stmt->getBody())) return true;

        std::string reduced{};
        llvm::raw_string_ostream reduced_stream(reduced);

        bool __cond_has_side_effects = __cond->HasSideEffects(m_ctx, /* includePossibleSideEffects = */ true);
        if (__cond_has_side_effects) {
            __cond->printPretty(reduced_stream, nullptr, m_policy);
            reduced_stream << ";";
        }

        reduced_stream.flush();

        m_last_update_loc_begin = while_stmt->getSourceRange().getBegin();
        m_last_update_loc_end = while_stmt->getSourceRange().getEnd();
        m_rewriter.ReplaceText(while_stmt->getSourceRange(), reduced);
    }

    return true;
}

bool deadCodeElimVisitor::VisitConditionalOperator(clang::ConditionalOperator* conditional_op) {
    if ((conditional_op->getSourceRange().getBegin() >= m_last_update_loc_begin) && (conditional_op->getSourceRange().getEnd() <= m_last_update_loc_end)) return true;

    clang::Expr* __cond = conditional_op->getCond();
    // allowSideEffects let statements like (a = 45, 45) pass which
    // are then caught later and added to the replacement string. 
    if(__cond->isValueDependent() || !(__cond->isEvaluatable(m_ctx, clang::Expr::SideEffectsKind::SE_AllowSideEffects)))
        return true;
    bool __cond_val;
    if (!__cond->EvaluateAsBooleanCondition(__cond_val, m_ctx)) return true;

    clang::Expr* result;
    if (__cond_val) result = conditional_op->getTrueExpr();
    else result = conditional_op->getFalseExpr();

    std::string reduced{};
    llvm::raw_string_ostream reduced_stream(reduced);

    if (clang::ImplicitCastExpr* ice = clang::dyn_cast<clang::ImplicitCastExpr>(result))
        reduced_stream << "(" << ice->getType().getAsString(m_policy) << ")";

    result->printPretty(reduced_stream, nullptr, m_policy);
    reduced_stream.flush();

    m_last_update_loc_begin = conditional_op->getSourceRange().getBegin();
    m_last_update_loc_end = conditional_op->getSourceRange().getEnd();
    m_rewriter.ReplaceText(conditional_op->getSourceRange(), reduced);
    return true;
}

// ─── LoopFlowCollector ──────────────────────────────────────────────

class LoopFlowCollector : public clang::RecursiveASTVisitor<LoopFlowCollector> {
private:
    bool have_loop_flow_stmts = false;
public:
    bool VisitBreakStmt(clang::BreakStmt* break_stmt) {
        have_loop_flow_stmts = true;
        return true;
    }
    bool VisitContinueStmt(clang::ContinueStmt* continue_stmt) {
        have_loop_flow_stmts = true;
        return true;
    }

    bool changes_loop_flow_semantics() {
        return have_loop_flow_stmts;
    }
};

bool deadCodeElimVisitor::VisitDoStmt(clang::DoStmt* do_stmt) {
    if ((do_stmt->getSourceRange().getBegin() >= m_last_update_loc_begin) && (do_stmt->getSourceRange().getEnd() <= m_last_update_loc_end)) return true;

    clang::Expr* __cond = do_stmt->getCond();
    // allowSideEffects let statements like (a = 45, 45) pass which
    // are then caught later and added to the replacement string. 
    if(__cond->isValueDependent() || !(__cond->isEvaluatable(m_ctx, clang::Expr::SideEffectsKind::SE_AllowSideEffects)))
        return true;
    bool __cond_val;
    if (!__cond->EvaluateAsBooleanCondition(__cond_val, m_ctx)) return true;
    if (!__cond_val) {
        LoopFlowCollector visitor;
        visitor.TraverseStmt(do_stmt->getBody());
        if(!visitor.changes_loop_flow_semantics()) {
            std::string reduced{};
            llvm::raw_string_ostream reduced_stream(reduced);
    
            do_stmt->getBody()->printPretty(reduced_stream, nullptr, m_policy);
    
            bool __cond_has_side_effects = __cond->HasSideEffects(m_ctx, /* includePossibleSideEffects = */ true);
            if (__cond_has_side_effects) {
                __cond->printPretty(reduced_stream, nullptr, m_policy);
                reduced_stream << ";";
            }
    
            reduced_stream.flush();
    
            m_last_update_loc_begin = do_stmt->getSourceRange().getBegin();
            m_last_update_loc_end = do_stmt->getSourceRange().getEnd();
            m_rewriter.ReplaceText(do_stmt->getSourceRange(), reduced);
        }
    }

    return true;
}

// ─── deadCodeElimCfgAnalysisVisitor ──────────────────────────────────────────────

deadCodeElimCfgAnalysisVisitor::deadCodeElimCfgAnalysisVisitor(clang::ASTContext& context, clang::Rewriter& rewriter) : m_ctx(context), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), did_remove(false) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool deadCodeElimCfgAnalysisVisitor::did_elim() {
    return did_remove;
}

bool deadCodeElimCfgAnalysisVisitor::has_label(clang::Stmt* stmt) {
    if(!stmt) return false;
    static LabelVerifier label_verifier;
    label_verifier.reset();
    label_verifier.TraverseStmt(stmt);
    return label_verifier.has_label_stmt();
}

void deadCodeElimCfgAnalysisVisitor::extend_unreachable_loc_for_parent(std::vector<std::pair<clang::SourceLocation, clang::SourceLocation>>& unreachable_locs, clang::Stmt* stmt) {    
    auto parents = m_ctx.getParents(*stmt);
    if (parents.empty()) return;

    const auto* parent_stmt = parents[0].get<clang::Stmt>();
    if (!parent_stmt) return;
    
    if (const clang::UnaryOperator* un_op = llvm::dyn_cast<clang::UnaryOperator>(parent_stmt); un_op && (un_op->getOpcode() == clang::UnaryOperator::Opcode::UO_Extension)) {
        unreachable_locs.push_back({un_op->getSourceRange().getBegin(), un_op->getSourceRange().getEnd()});

        parents = m_ctx.getParents(*un_op);
        if (parents.empty()) return;

        parent_stmt = parents[0].get<clang::Stmt>();
        if (!parent_stmt) return;
    }
    
    if (const clang::ParenExpr* paren_expr = llvm::dyn_cast<clang::ParenExpr>(parent_stmt)) {
        paren_expr->dump();
        const clang::Expr* current = paren_expr;

        while (true) {
            auto parents = m_ctx.getParents(*current);
            if (parents.empty()) break;

            const auto* parent_stmt = parents[0].get<clang::Stmt>();
            if (!parent_stmt) break;

            // strip outer parens if any
            if (const auto* paren = llvm::dyn_cast<clang::ParenExpr>(parent_stmt)) {
                current = paren;
                continue;
            }

            break;
        }
        unreachable_locs.push_back({current->getSourceRange().getBegin(), current->getSourceRange().getEnd()});
    }
}

bool deadCodeElimCfgAnalysisVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody()) return true;
    if (!func->isThisDeclarationADefinition()) return true;

    m_cfg_options.AddScopes = true;
    std::unique_ptr<clang::CFG> mapped_cfg = clang::CFG::buildCFG(func, func->getBody(), &m_ctx, m_cfg_options);
    if(!mapped_cfg) return true;
    mapped_cfg->dump(m_ctx.getLangOpts(), true);
    std::unordered_map<clang::CFGBlock*, bool> reachable_block;
    std::queue<clang::CFGBlock*> reachability_queue;
    std::unordered_map<clang::CFGBlock*, bool> block_visited;

    for (clang::CFGBlock *_block : *mapped_cfg) {
        reachable_block[_block] = false;
        block_visited[_block] = false;
    }

    reachability_queue.push(&(mapped_cfg->getEntry()));
    block_visited[&(mapped_cfg->getEntry())] = true;
    reachable_block[&(mapped_cfg->getEntry())] = true;

    while (!reachability_queue.empty()) {
        clang::CFGBlock *_block = reachability_queue.front();
        reachability_queue.pop();
        for (auto succ : _block->succs()) {
            if (succ and !block_visited[succ]) {
                block_visited[succ] = true;
                reachable_block[succ] = true;
                reachability_queue.push(succ);
            }
        }
    }

    // do not remove a control flow block that introduces variables which might be
    // used later on in other control flow blocks that are live. This behavior is 
    // present when goto expressions mess up the program flow. 
    for(auto block_status : reachable_block) {
        if (!block_status.second) {
            clang::CFGBlock* _block = block_status.first;

            /**
             * The intuition here is that we want to see if any variable that was introduced
             * in this block escapes the block. That is possible if we don't see a conforming
             * scopeEnd for the variable. 
             * 
             * Now multiples Decls can exist, so we maintain a stack and : 
             * 
             * 1. Push a Decl if only top is not a Decl
             * 2. Push a SCOPE_BEGIN marker unconditionally
             * 3. When a scopeEnd stmt is encountered, there are a couple cases. Either the state
             *    is empty which is fine. Either top is a Decl but its ScopeBegin is not there. Its
             *    in some other block but that's also fine. Or else top is a Decl and just below it
             *    is a BeginStmt. In this case we remove two elements from the stack. 
             * 4. At the end if the scope_state is not empty, that is some Decls which were introduced are
             *    leaking, we mark the block as reachable. 
             */
            enum scope_info {
                DECL,
                SCOPE_BEGIN,
            };

            std::stack<scope_info> scope_state;

            for (clang::CFGElement &Elem : *_block) {
                if (auto cfg_stmt = Elem.getAs<clang::CFGStmt>()) {
                    if (clang::Stmt* stmt = const_cast<clang::Stmt*>(cfg_stmt->getStmt())) {
                        if (llvm::dyn_cast<clang::DeclStmt>(stmt)) {
                            if(!scope_state.empty()) {
                                if(scope_state.top() == SCOPE_BEGIN) {
                                    scope_state.push(DECL);
                                }
                            } else {
                                scope_state.push(DECL);
                            }
                        }
                    }
                } else if (auto scope_end = Elem.getAs<clang::CFGScopeEnd>()) {
                    if(scope_state.empty()) continue;
                    scope_state.pop();
                    if(scope_state.size()) scope_state.pop();
                } else if (auto scope_begin = Elem.getAs<clang::CFGScopeBegin>()) {
                    scope_state.push(SCOPE_BEGIN);
                }
            }

            if(!scope_state.empty()) reachable_block[_block] = true;
        }
    }

    std::vector<std::pair<clang::SourceLocation, clang::SourceLocation>> unreachable_locs;
    for(auto block_status : reachable_block) {
        if (!block_status.second) {
            clang::CFGBlock* _block = block_status.first;
            bool terminator_has_label_stmt = false;
            if (clang::Stmt* terminator_stmt = _block->getTerminatorStmt()) {
                terminator_has_label_stmt = has_label(terminator_stmt);
                if (!terminator_has_label_stmt) {
                    unreachable_locs.push_back({terminator_stmt->getSourceRange().getBegin(), terminator_stmt->getSourceRange().getEnd()});
                }
            }
            for(auto Elem = _block->begin(); Elem < _block->end(); Elem++) {
                if(terminator_has_label_stmt) {
                    if (Elem == (_block->end() - 1)) continue;
                }
                if (auto cfg_stmt = Elem->getAs<clang::CFGStmt>()) {
                    if (clang::Stmt* stmt = const_cast<clang::Stmt*>(cfg_stmt->getStmt())) {
                        // some statements need more cleanup than the info just present
                        // in the cfgStmt. 
                        // 1. binaryOperator(,) if has parenExpr as immediate parent,
                        //    we should include the parent in unreachable loc. The ParenExpr
                        //    information is not present in the CFGStmt. 
                        // 2. UnaryOperator(__extension__) can occur as a parent for special
                        //    nodes. We don't keep track of the special nodes and instead check
                        //    unconditionally for it. This is because its occurence in non special 
                        //    nodes is not technically an error. This is not modelled as a special 
                        //    CFGStmt in the clang API and so we add a special check for it. 
                        extend_unreachable_loc_for_parent(unreachable_locs, stmt);
                        unreachable_locs.push_back({stmt->getSourceRange().getBegin(), stmt->getSourceRange().getEnd()});
                    }
                }
            }
        }
    }

    if(unreachable_locs.empty()) return true;
    did_remove = true;

    std::sort(unreachable_locs.begin(), unreachable_locs.end(), [](std::pair<clang::SourceLocation, clang::SourceLocation> a, std::pair<clang::SourceLocation, clang::SourceLocation> b) {
        return a.first.getRawEncoding() < b.first.getRawEncoding();
    });

    std::vector<std::pair<clang::SourceLocation, clang::SourceLocation>> merged_unreachable_locs;

    clang::SourceLocation rewrite_min_unreachable_loc = unreachable_locs[0].first;
    unsigned min_unreachable_loc = unreachable_locs[0].first.getRawEncoding();

    clang::SourceLocation rewrite_max_unreachable_loc = unreachable_locs[0].second;
    unsigned max_unreachable_loc = unreachable_locs[0].second.getRawEncoding();

    for(int i = 1; i < unreachable_locs.size(); i++) {
        unsigned start = unreachable_locs[i].first.getRawEncoding();
        unsigned end = unreachable_locs[i].second.getRawEncoding();

        // overlap if the start lies in between the max and min unreachable_loc
        if((min_unreachable_loc <= start) and (start <= max_unreachable_loc)) {
            max_unreachable_loc = std::max(end, max_unreachable_loc);
            if (end == max_unreachable_loc)
                rewrite_max_unreachable_loc = unreachable_locs[i].second;
        } else {
            merged_unreachable_locs.push_back({rewrite_min_unreachable_loc, rewrite_max_unreachable_loc});
            min_unreachable_loc = start;
            max_unreachable_loc = end;
            rewrite_min_unreachable_loc = unreachable_locs[i].first;
            rewrite_max_unreachable_loc = unreachable_locs[i].second;
        }
    }
    merged_unreachable_locs.push_back({rewrite_min_unreachable_loc, rewrite_max_unreachable_loc});

    for(auto& unreachable_loc : merged_unreachable_locs)
        m_rewriter.RemoveText({unreachable_loc.first, unreachable_loc.second});

    return true;
}

// ─── transferStmtVisitor ──────────────────────────────────────────────

transferStmtVisitor::transferStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, clang::PrintingPolicy policy, reaching_copies_t& copies, bool do_updates, bool& did_propagate, std::unordered_map<unsigned, bool>& source_loc_updated) : m_ctx(ctx), m_rewriter(rewriter), m_policy(policy), m_copies(copies), m_do_updates(do_updates), m_did_propagate(did_propagate), m_source_loc_updated(source_loc_updated) { }

bool transferStmtVisitor::shouldTraversePostOrder() { return true; }

bool transferStmtVisitor::is_primitive_type(clang::QualType qt) {
  if (qt.isVolatileQualified()) return false;
  qt = qt.getCanonicalType();

  while (qt->isPointerType())
    qt = qt->getPointeeType().getCanonicalType();

  if (qt->isEnumeralType())  return false;
  if (qt->isRecordType())    return false;

  return qt->isArithmeticType();
}

void transferStmtVisitor::possibly_add_viable_copy(clang::VarDecl* var_decl, clang::Expr* expr) {
    if(!expr || !var_decl) return;

    expr = expr->IgnoreParenLValueCasts();
    if (clang::VarDecl* expr_identifier = extract_var_decl(expr)) {
        // guard against adding a copy that references the 
        // identifier of the base variable itself. 
        // ie. x = x
        if (var_decl != expr_identifier) {
            reaching_copies_t::copy_target_t target;
            target.m_identifier = expr_identifier;
            target.m_has_storage = true;

            m_copies.add_copies({var_decl, target});
        }
    } else if (!(expr->isValueDependent()) && expr->isEvaluatable(m_ctx, clang::Expr::SideEffectsKind::SE_AllowSideEffects)) {
        // guard against certain init types. This list will only grow with time. 
        // 1. StringLiterals
        // 2. InitListExpr
        // 3. calls to builtin functions which can be folded to a constant (eg. inf)
        // 4. pointer types initialized with a constant (generally 0)
        if (clang::Expr* stripped_expr = expr->IgnoreParenCasts()) {
            if(llvm::dyn_cast<clang::StringLiteral>(stripped_expr)) {
                return;
            } else if(llvm::dyn_cast<clang::InitListExpr>(stripped_expr)) {
                return;
            } else if (auto* call = llvm::dyn_cast<clang::CallExpr>(stripped_expr)) {
                if (auto* func_decl = llvm::dyn_cast_or_null<clang::FunctionDecl>(call->getCalleeDecl())) {
                    if (func_decl->getBuiltinID() != 0) {
                        return;
                    }
                }
            }
        }

        if (var_decl->getType()->isPointerType()) return;

        clang::Expr::EvalResult result;
        if(!expr->EvaluateAsRValue(result, m_ctx)) return;

        clang::APValue init_value = result.Val;
        reaching_copies_t::copy_target_t target;
        target.m_numeric_constant = init_value;
        target.m_has_storage = false;

        m_copies.add_copies({var_decl, target});
    }
}

bool transferStmtVisitor::is_dereference_operation(clang::Expr* expr) {
    if(llvm::dyn_cast<clang::ArraySubscriptExpr>(expr)) return true;
    if(clang::UnaryOperator* unary_op = llvm::dyn_cast<clang::UnaryOperator>(expr)) {
        if (unary_op->getOpcode() == clang::UnaryOperator::Opcode::UO_Deref) return true;
    }
    return false;
}

clang::VarDecl* transferStmtVisitor::extract_var_decl(clang::Expr* expr) {
    if (clang::DeclRefExpr* decl_ref_expr = llvm::dyn_cast<clang::DeclRefExpr>(expr->IgnoreParens())) {
        if (clang::VarDecl *var_decl = llvm::dyn_cast<clang::VarDecl>(decl_ref_expr->getDecl())) {
            return var_decl;
        }
    }
    return nullptr;
}

void transferStmtVisitor::transfer_vars_in_type(clang::QualType T) {
    const clang::Type *TP = T.getTypePtr();

    if (const auto *TOE = llvm::dyn_cast<clang::TypeOfExprType>(TP)) {
        if (clang::Expr *expr = TOE->getUnderlyingExpr()) {
            if (m_do_updates) {
                if (clang::VarDecl* var_decl = extract_var_decl(expr->IgnoreParens())) {
                    if(is_primitive_type(var_decl->getType())) {
                        if (!m_source_loc_updated[expr->getSourceRange().getBegin().getRawEncoding()]) {
                            if(std::string to_copy = m_copies.find_if_copy_exists(var_decl, m_ctx); !to_copy.empty()) {
                                to_copy = "(" + to_copy + ")";
                                llvm::outs() << "Replacing " << var_decl->getNameAsString() << " with " << to_copy << "\n";
                                m_rewriter.ReplaceText(expr->getSourceRange(), to_copy);
                                m_did_propagate = true;
                                m_source_loc_updated[expr->getSourceRange().getBegin().getRawEncoding()] = true;
                            }
                        }
                    }
                }
            }
            this->TraverseStmt(expr);
        }
        transfer_vars_in_type(TOE->desugar());
        return;
    } else if (const auto *TOT = llvm::dyn_cast<clang::TypeOfType>(TP)) {
        transfer_vars_in_type(TOT->desugar());
        return;
    }

    if (const auto *VAT = llvm::dyn_cast<clang::VariableArrayType>(TP)) {
        if (clang::Expr *size = VAT->getSizeExpr()) {
            if (m_do_updates) {
                if (clang::VarDecl* var_decl = extract_var_decl(size->IgnoreParens())) {
                    if(is_primitive_type(var_decl->getType())) {
                        if (!m_source_loc_updated[size->getSourceRange().getBegin().getRawEncoding()]) {
                            if(std::string to_copy = m_copies.find_if_copy_exists(var_decl, m_ctx); !to_copy.empty()) {
                                to_copy = "(" + to_copy + ")";
                                llvm::outs() << "Replacing " << var_decl->getNameAsString() << " with " << to_copy << "\n";
                                m_rewriter.ReplaceText(size->getSourceRange(), to_copy);
                                m_did_propagate = true;
                                m_source_loc_updated[size->getSourceRange().getBegin().getRawEncoding()] = true;
                            }
                        }
                    }
                }
            }
            this->TraverseStmt(size);
        }
        transfer_vars_in_type(VAT->getElementType());
    } else if (const auto *PT = llvm::dyn_cast<clang::PointerType>(TP)) {
        transfer_vars_in_type(PT->getPointeeType());
    } else if (const auto *RT = llvm::dyn_cast<clang::ReferenceType>(TP)) {
        transfer_vars_in_type(RT->getPointeeType());
    } else if (const auto *AT = llvm::dyn_cast<clang::ArrayType>(TP)) {
        transfer_vars_in_type(AT->getElementType());
    } else if (const auto *FPT = llvm::dyn_cast<clang::FunctionProtoType>(TP)) {
        transfer_vars_in_type(FPT->getReturnType());
        for (auto paramType : FPT->param_types())
            transfer_vars_in_type(paramType);
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        transfer_vars_in_type(FT->getReturnType());
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        transfer_vars_in_type(FT->getReturnType());
    } else if (const auto *MPT = llvm::dyn_cast<clang::MemberPointerType>(TP)) {
        transfer_vars_in_type(MPT->getPointeeType());
    } else if (const auto *VT = llvm::dyn_cast<clang::VectorType>(TP)) {
        transfer_vars_in_type(VT->getElementType());
    } else if (const auto *ParenT = llvm::dyn_cast<clang::ParenType>(TP)) {
        transfer_vars_in_type(ParenT->getInnerType());
    }
}

bool transferStmtVisitor::VisitBinaryOperator(clang::BinaryOperator* binary_op) {
    // if its an assignment type op, invalidate the copy of the variable
    // on the lhs. If the rhs is a copyable expr, store it. 
    if (binary_op->isAssignmentOp()) {
        clang::Expr* lhs = binary_op->getLHS()->IgnoreParens();
        if (is_primitive_type(lhs->getType())) {
            if (clang::VarDecl* identifier = extract_var_decl(lhs)) {
                m_copies.kill_copies_that_use_identifier(identifier);
                if (binary_op->getOpcode() == clang::BO_Assign) {
                    possibly_add_viable_copy(identifier, binary_op->getRHS());
                }
            } else if (is_dereference_operation(lhs)) {
                m_copies.kill_copies_with_global_storage();
                m_copies.kill_copies_that_can_alias();
            }
        }
    }

    return true;
}

bool transferStmtVisitor::VisitImplicitCastExpr(clang::ImplicitCastExpr* ice) {
  if(!m_do_updates) return true;
  if (ice->getCastKind() == clang::CK_LValueToRValue) {
    if (clang::DeclRefExpr* decl_ref_expr = llvm::dyn_cast<clang::DeclRefExpr>(ice->getSubExpr()->IgnoreParens())) {
        if (m_source_loc_updated[decl_ref_expr->getSourceRange().getBegin().getRawEncoding()]) return true;
        if(is_primitive_type(decl_ref_expr->getType())) {
            if (clang::VarDecl *var_decl = llvm::dyn_cast<clang::VarDecl>(decl_ref_expr->getDecl())) {
                if(std::string to_copy = m_copies.find_if_copy_exists(var_decl, m_ctx); !to_copy.empty()) {
                    llvm::outs() << "Replacing " << var_decl->getNameAsString() << " with " << to_copy << "\n";
                    m_rewriter.ReplaceText(decl_ref_expr->getSourceRange(), to_copy);
                    m_did_propagate = true;
                    m_source_loc_updated[decl_ref_expr->getSourceRange().getBegin().getRawEncoding()] = true;
                }
            }
        }
    }
  }
  return true;
}

bool transferStmtVisitor::VisitDeclStmt(clang::DeclStmt* decl_stmt) {
    for (clang::Decl* decl : decl_stmt->decls()) {
        if (clang::VarDecl* var_decl = llvm::dyn_cast<clang::VarDecl>(decl)) {
            transfer_vars_in_type(var_decl->getType());
            if(is_primitive_type(var_decl->getType())) {
                possibly_add_viable_copy(var_decl, var_decl->getInit());
            }
        }
    }
    return true;
}

bool transferStmtVisitor::VisitUnaryOperator(clang::UnaryOperator* unary_op) {
    typedef clang::UnaryOperator::Opcode un_opcode;
    un_opcode unop = unary_op->getOpcode();
    /**
     * possible flavors => 
     *     1. post/pre inc/dec
     *     2. neg/not/lnot
     *     3. AddrOf/Deref
     *     4. [Not handled] co_await / complex / extension marker
     */
    // remove the target variable from the list of copies if it exists
    if (unop >= un_opcode::UO_PostInc && unop <= un_opcode::UO_PreDec) {
        clang::Expr* inner_expr = unary_op->getSubExpr()->IgnoreParens();
        if (clang::VarDecl* identifier = extract_var_decl(inner_expr)) {
            if(is_primitive_type(identifier->getType())) {
                m_copies.kill_copies_that_use_identifier(identifier);
            }
        }
    }

    // mark the target variable as a possible aliasing target
    if (unop == un_opcode::UO_AddrOf) {
        clang::Expr* inner_expr = unary_op->getSubExpr()->IgnoreParens();
        if (clang::VarDecl* identifier = extract_var_decl(inner_expr)) {
            if(is_primitive_type(identifier->getType())) {
                m_copies.add_possible_alias_target(identifier);
            }
        }
    }

    return true;
}

bool transferStmtVisitor::VisitCallExpr(clang::CallExpr* call) {
    // mark all copies that refer a variable that has global storage
    // or variables whose addresses are taken as invalid. 
    m_copies.kill_copies_with_global_storage();
    m_copies.kill_copies_that_can_alias();

    return true;
}

bool transferStmtVisitor::VisitArraySubscriptExpr(clang::ArraySubscriptExpr* ase) {
    if(!m_do_updates) return true;
    if (clang::VarDecl* var_decl = extract_var_decl(ase->getBase()->IgnoreParenCasts())) {
        if(is_primitive_type(var_decl->getType())) {
            if (m_source_loc_updated[ase->getBase()->getSourceRange().getBegin().getRawEncoding()]) return true;
            if(std::string to_copy = m_copies.find_if_copy_exists(var_decl, m_ctx); !to_copy.empty()) {
                llvm::outs() << "Replacing " << var_decl->getNameAsString() << " with " << to_copy << "\n";
                m_rewriter.ReplaceText(ase->getBase()->getSourceRange(), to_copy);
                m_did_propagate = true;
                m_source_loc_updated[ase->getBase()->getSourceRange().getBegin().getRawEncoding()] = true;
            }
        }
    }
    return true;
}

bool transferStmtVisitor::VisitGCCAsmStmt(clang::GCCAsmStmt* asm_stmt) {
    for (unsigned i = 0; i < asm_stmt->getNumOutputs(); ++i) {
        if (clang::VarDecl* identifier = extract_var_decl(asm_stmt->getOutputExpr(i))) {
            if(is_primitive_type(identifier->getType())) {
                m_copies.kill_copies_that_use_identifier(identifier);
            }
        }
    }

    return true;
}

bool transferStmtVisitor::TraverseStmtExpr(clang::StmtExpr* stmt_expr) {
    return true;
}

bool transferStmtVisitor::TraverseBinaryOperator(clang::BinaryOperator* bin_op) {
    if (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Comma) return true;
    else return clang::RecursiveASTVisitor<transferStmtVisitor>::TraverseBinaryOperator(bin_op);
}

// ─── varsInScopeCollector ──────────────────────────────────────────────

class varsInScopeCollector : public clang::RecursiveASTVisitor<varsInScopeCollector> {
private:
    std::unordered_map<clang::VarDecl*, std::vector<clang::VarDecl*>> m_scope_anchors;
public:
    bool VisitCompoundStmt(clang::CompoundStmt* compound_stmt) {
        bool is_first_decl_stmt = true;
        clang::VarDecl* anchor;
        for (clang::Stmt* child : compound_stmt->body()) {
            if (auto decl_stmt = llvm::dyn_cast<clang::DeclStmt>(child)) {
                for (auto decl : decl_stmt->decls()) {
                    if (auto var_decl = llvm::dyn_cast<clang::VarDecl>(decl)) {
                        if (is_first_decl_stmt) {
                            anchor = var_decl;
                            is_first_decl_stmt = false;
                        }
                        m_scope_anchors[anchor].push_back(var_decl);
                    }
                }
            }
        }

        return true;
    }

    std::unordered_map<clang::VarDecl*, std::vector<clang::VarDecl*>> get_scope_anchors() {
        return m_scope_anchors;
    }
};

// ─── copyPropagationVisitor ──────────────────────────────────────────────

copyPropagationVisitor::copyPropagationVisitor(clang::ASTContext& context, clang::Rewriter& rewriter) : m_ctx(context), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_did_propagate(false) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    m_policy.SuppressTagKeyword = false;
}

bool copyPropagationVisitor::did_propagate() {
    return m_did_propagate;
}

void copyPropagationVisitor::meet(reaching_copies_t& merge_into, const reaching_copies_t& merge_from) {
    // top, hence contain all copies that the TU can ever have. 
    // intersection with merge_into would result in merge_into. 
    if (merge_from.is_top()) return;
    // if merge_into is top, then intersection with merge_from
    // would result in merge_from. Hence we can copy early and
    // return. 
    if (merge_into.is_top()) {
        merge_into = merge_from;
        return;
    }
    // transfer the variables that can alias unconditionally. We want to be 
    // conservative here. 
    merge_into.merge_vars_that_can_alias(merge_from);

    auto& into = merge_into.m_copies;
    const auto& from = merge_from.m_copies;

    // retain copies that are semantically same 
    std::vector<reaching_copies_t::copy_t> result;
    for (const auto& a : from) {
        for (const auto& b : into) {
            if (a.is_same_as(b)) { result.push_back(a); break; }
        }
    }
    into = std::move(result);
}

void copyPropagationVisitor::transfer(reaching_copies_t& copies, clang::Stmt* stmt, bool do_updates, std::unordered_map<unsigned, bool>& source_loc_updated) {
    /**
     * TODO: The analysis is currently only for primtive types. 
     *       Extend it for Arrays and Records. 
     * 1. VarDecl -> check if init has a DeclRefExpr that contains
     *               a single varDecl or an Expr that can be reduced
     *               to a constant. If so, add the identifier to the
     *               list of reaching_copies. 
     * 2. BinaryOp              -> check rhs for possible rewrites of 
     *    (Assignment Variants)    identifiers. Destroy copy that 
     *                             intersects with lhs. 
     * 3. unaryOp -> check lvalue on which op happens and destroy copy
     *               that intersects. 
     * 4. CallExpr -> invalidate copies for variables that either have 
     *                global storage or posses aliasing potential. 
     * 5. unaryOp -> Add the variable to the list of variables that can 
     *    (AddrOf)   alias. 
     * 6. unaryOp -> if it appears in the lhs of an assignment operator,
     *    (DeRef)    destroy all copies that contain variables that can alias.
     * 7. ArraySubscriptExpr -> if it appears in the lhs of an assignment operator,
     *                          Destroy all copies that contain variables that can alias. 
     * [Note: For 6 and 7, if the ptr is to shift and its not an array, then its undefined]
     * [      behavior and the standard allows us to optimize it away however we see fit. ]
     * [      This intenionally incorrect behavior is observed in both gcc and clang      ]
     * [      For 7, we don't keep track of which pointer points to which variable and so ]
     * [      we have no way of knowing if the arraySubscriptOp was not used for a var ptr]
     * [      For records to have any effect on the local/param variables, they would have]
     * [      to take their address first and 5 thus then take care of the correctness    ]
     * [      Finally note that we do updates conditionally. The algorithm first uses     ]
     * [      transfer function to pin down a reaching_copies set for each cfg. Once its  ]
     * [      set, it uses the same logic, only this time to make changes on the go       ]
     * 8. ImplicitCastExpr (lvalue-to-rvalue) +
     *           DeclRefExpr + VarDecl         -> these represent usage of a variable that 
     *                                            has storage in an expression. Proper usage
     *                                            would entail converting the lvalue to an 
     *                                            rvalue. Then check if a valid copy for this
     *                                            identifier exists and do the replacement. 
     */
    transferStmtVisitor visitor(m_ctx, m_rewriter, m_policy, copies, do_updates, m_did_propagate, source_loc_updated);
    visitor.TraverseStmt(stmt);
}

bool copyPropagationVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody()) return true;
    if (!func->isThisDeclarationADefinition()) return true;

    m_cfg_options.AddScopes = true;
    std::unique_ptr<clang::CFG> mapped_cfg = clang::CFG::buildCFG(func, func->getBody(), &m_ctx, m_cfg_options);
    if(!mapped_cfg) return true;
    mapped_cfg->dump(m_ctx.getLangOpts(), true);
    llvm::outs() << "==============================================\n\n";

    std::deque<clang::CFGBlock*> work_queue;
    work_queue.push_back(&(mapped_cfg->getEntry()));
    std::map<clang::CFGBlock*, reaching_copies_t> block_copies_map;
    std::unordered_map<unsigned, bool> source_loc_updated;
    varsInScopeCollector visitor;
    visitor.TraverseStmt(func->getBody());
    auto scope_anchors = visitor.get_scope_anchors();

    for (clang::CFGBlock *_block : *mapped_cfg) {
        block_copies_map[_block] = {};
    }

    /**
     * Block Traversal Strategy :-
     * 
     * (Following lattice terminology)
     * Top reaching_copies : initialize all reaching_copies with their 
     *                          top equivalent. Ie. initially, each block's
     *                          reaching_copies set include all the copies 
     *                          that can ever exist in a TU. 
     * Bottom reaching_copies : An empty set for the reaching_copies. 
     * 
     * In the beginning, the reaching_copies of each basic block is marked
     * top. So that in the meet function, it does not matter whether some
     * predecessor was processed or not. Intersection of X with universal
     * set is X itself. Once meet operation is done, we transfer the copies
     * within each block. This is an isolated operation. But we still keep
     * track of which source locations were updated (in the second traversal)
     * because there can be instances when the clangCFG Block has multiple
     * CFGElems, some of which are children of their peer CFGElems. 
     * This can lead to multiple updates at the same place for the
     * last statement of a CFGBlock. This is to guard against that.
     * Moving on to adding new blocks to our worklist. Now if the current
     * block was never visited before, then its successors would definitely
     * be added. Because Its topness would have definitely changed and its
     * possible that some of its successors were processed before this block. 
     * So since the topness have changed now, we must process these successors
     * again. Instead if the block was already visited and we are visiting it again
     * then we conditionally add successors only if the reaching_copies set changed
     * for the current block. The second iteration on the other hand is more of a
     * question of which block is reachable and so we add successors depending on 
     * whether they have been visited or not. The reaching_copies set for each block
     * will anyways remain constant. 
     */

    // first stabilize the reaching_copies set for each basic block
    while (!work_queue.empty()) {
        clang::CFGBlock *_block = work_queue.front();
        work_queue.pop_front();

        // meet function merges the copies
        // coming from the block's predecessors
        reaching_copies_t reaching_copies;
        for (auto pred : _block->preds()) {
            if (!pred) continue;
            meet(reaching_copies, block_copies_map[pred]);
        }

        // transfer function changes the current copies
        // to take statements in the current block into account
        for (clang::CFGElement &Elem : *_block) {
            if (auto cfg_stmt = Elem.getAs<clang::CFGStmt>()) {
                if (clang::Stmt* stmt = const_cast<clang::Stmt*>(cfg_stmt->getStmt())) {
                    // guard against certain node types that can contain complex SubStatements. 
                    // While clangCFG takes care of compoundStmt, the same is not true for 
                    // others. This is required else these subStatements will be run the
                    // transfer function in a backward analysis fashion which is just wrong. 
                    // The expressions / statements in such nodes appear further up in the 
                    // CFG Block and so we can just skip them. 
                    if (llvm::dyn_cast<clang::StmtExpr>(stmt)) continue;
                    if (clang::BinaryOperator* bin_op = llvm::dyn_cast<clang::BinaryOperator>(stmt); bin_op && (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Comma)) continue;
                    transfer(reaching_copies, stmt, false, source_loc_updated);
                }
            } else if (auto scope_end = Elem.getAs<clang::CFGScopeEnd>()) {
                for(auto var_decl : scope_anchors[const_cast<clang::VarDecl*>(scope_end->getVarDecl())]) {
                    reaching_copies.kill_copies_that_use_identifier(var_decl);
                }
            }
        }
        if (clang::Stmt* stmt = _block->getTerminatorStmt()) {
            if (auto asm_stmt = llvm::dyn_cast<clang::GCCAsmStmt>(stmt)) {
                transfer(reaching_copies, asm_stmt, false, source_loc_updated);
            }
        }
        reaching_copies.mark_is_not_top();

        bool reaching_copies_has_changed = reaching_copies != block_copies_map[_block];

        for (auto succ : _block->succs()) {
            if (!succ) continue;
            if (reaching_copies_has_changed) {
                if (std::find(work_queue.begin(), work_queue.end(), succ) == work_queue.end()) {
                    work_queue.push_back(succ);
                }
            }
        }

        if (reaching_copies_has_changed) 
            block_copies_map[_block] = reaching_copies;
    }

    llvm::outs() << "Running the second iteration\n";

    // once the reaching_copies are stabilized, run the algorithm again,
    // only this time, allow the transfer function to do updates
    work_queue.push_back(&(mapped_cfg->getEntry()));
    std::unordered_map<clang::CFGBlock*, bool> block_visited;

    while (!work_queue.empty()) {
        clang::CFGBlock *_block = work_queue.front();
        work_queue.pop_front();

        // meet function merges the copies
        // coming from the block's predecessors
        reaching_copies_t reaching_copies;
        bool first_pred_guard = true;
        for (auto pred : _block->preds()) {
            if (!pred) continue;
            meet(reaching_copies, block_copies_map[pred]);
        }

        // transfer function changes the current copies
        // to take statements in the current block into account
        for (clang::CFGElement &Elem : *_block) {
            if (auto cfg_stmt = Elem.getAs<clang::CFGStmt>()) {
                if (clang::Stmt* stmt = const_cast<clang::Stmt*>(cfg_stmt->getStmt())) {
                    // guard against certain node types that can contain complex SubStatements. 
                    // While clangCFG takes care of compoundStmt, the same is not true for 
                    // others. This is required else these subStatements will be run the
                    // transfer function in a backward analysis fashion which is just wrong. 
                    // The expressions / statements in such nodes appear further up in the 
                    // CFG Block and so we can just skip them. 
                    if (llvm::dyn_cast<clang::StmtExpr>(stmt)) continue;
                    if (clang::BinaryOperator* bin_op = llvm::dyn_cast<clang::BinaryOperator>(stmt); bin_op && (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Comma)) continue;
                    transfer(reaching_copies, stmt, true, source_loc_updated);
                }
            } else if (auto scope_end = Elem.getAs<clang::CFGScopeEnd>()) {
                for(auto var_decl : scope_anchors[const_cast<clang::VarDecl*>(scope_end->getVarDecl())]) {
                    reaching_copies.kill_copies_that_use_identifier(var_decl);
                }
            }
        }
        if (clang::Stmt* stmt = _block->getTerminatorStmt()) {
            if (auto asm_stmt = llvm::dyn_cast<clang::GCCAsmStmt>(stmt)) {
                transfer(reaching_copies, asm_stmt, true, source_loc_updated);
            }
        }

        reaching_copies.mark_is_not_top();
        assert(reaching_copies == block_copies_map[_block] && "reaching_copies set should not change when the transfer function is allowed to make changes");

        block_visited[_block] = true;
        for (auto succ : _block->succs()) {
            if (!succ) continue;
            // now we check whether the block has been visited or not
            // because transfer function is isolated to a block. Hence
            // the changes it is supposed to do should happen only once. 
            if (!block_visited[succ]) {
                if(std::find(work_queue.begin(), work_queue.end(), succ) == work_queue.end()) {
                    work_queue.push_back(succ);
                }
            }
        }
    }

    return true;
}

// ─── transferLivenessStmtVisitor ──────────────────────────────────────────────

transferLivenessStmtVisitor::transferLivenessStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, clang::PrintingPolicy policy, live_variables_t& live_variables, bool do_updates, bool& did_elim, std::unordered_map<unsigned, bool>& source_loc_updated, std::unordered_map<clang::VarDecl*, bool>& aliased_and_global_vars) : m_ctx(ctx), m_rewriter(rewriter), m_policy(policy), m_live_variables(live_variables), m_do_updates(do_updates), m_did_elim(did_elim), m_source_loc_updated(source_loc_updated), m_aliased_and_global_vars(aliased_and_global_vars) { }

bool transferLivenessStmtVisitor::is_primitive_type(clang::QualType qt) {
  if (qt.isVolatileQualified()) return false;
  qt = qt.getCanonicalType();

  while (qt->isPointerType())
    qt = qt->getPointeeType().getCanonicalType();

  if (qt->isEnumeralType())  return false;
  if (qt->isRecordType())    return false;

  return qt->isArithmeticType();
}

clang::VarDecl* transferLivenessStmtVisitor::extract_var_decl(clang::Expr* expr) {
    if (clang::DeclRefExpr* decl_ref_expr = llvm::dyn_cast<clang::DeclRefExpr>(expr->IgnoreParens())) {
        if (clang::VarDecl *var_decl = llvm::dyn_cast<clang::VarDecl>(decl_ref_expr->getDecl())) {
            return var_decl;
        }
    }
    return nullptr;
}

void transferLivenessStmtVisitor::recurse_through_type(clang::QualType T) {
    const clang::Type *TP = T.getTypePtr();

    if (const auto *TOE = llvm::dyn_cast<clang::TypeOfExprType>(TP)) {
        if (clang::Expr *expr = TOE->getUnderlyingExpr()) {
            if (clang::VarDecl* identifier = extract_var_decl(expr->IgnoreParens())) {
                if(is_primitive_type(identifier->getType())) {
                    m_live_variables.record_usage(identifier);
                }
            }
            this->TraverseStmt(expr);
        }
        recurse_through_type(TOE->desugar());
        return;
    } else if (const auto *TOT = llvm::dyn_cast<clang::TypeOfType>(TP)) {
        recurse_through_type(TOT->desugar());
        return;
    }

    if (const auto *VAT = llvm::dyn_cast<clang::VariableArrayType>(TP)) {
        if (clang::Expr *size = VAT->getSizeExpr()) {
            if (clang::VarDecl* identifier = extract_var_decl(size->IgnoreParens())) {
                if(is_primitive_type(identifier->getType())) {
                    m_live_variables.record_usage(identifier);
                }
            }
            this->TraverseStmt(size);
        }
        recurse_through_type(VAT->getElementType());
    } else if (const auto *PT = llvm::dyn_cast<clang::PointerType>(TP)) {
        recurse_through_type(PT->getPointeeType());
    } else if (const auto *RT = llvm::dyn_cast<clang::ReferenceType>(TP)) {
        recurse_through_type(RT->getPointeeType());
    } else if (const auto *AT = llvm::dyn_cast<clang::ArrayType>(TP)) {
        recurse_through_type(AT->getElementType());
    } else if (const auto *FPT = llvm::dyn_cast<clang::FunctionProtoType>(TP)) {
        recurse_through_type(FPT->getReturnType());
        for (auto paramType : FPT->param_types())
            recurse_through_type(paramType);
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        recurse_through_type(FT->getReturnType());
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        recurse_through_type(FT->getReturnType());
    } else if (const auto *MPT = llvm::dyn_cast<clang::MemberPointerType>(TP)) {
        recurse_through_type(MPT->getPointeeType());
    } else if (const auto *VT = llvm::dyn_cast<clang::VectorType>(TP)) {
        recurse_through_type(VT->getElementType());
    } else if (const auto *ParenT = llvm::dyn_cast<clang::ParenType>(TP)) {
        recurse_through_type(ParenT->getInnerType());
    }
}

bool transferLivenessStmtVisitor::VisitBinaryOperator(clang::BinaryOperator* binary_op) {
    if (binary_op->isAssignmentOp()) {
        clang::Expr* lhs = binary_op->getLHS()->IgnoreParens();
        if (is_primitive_type(lhs->getType())) {
            if (clang::VarDecl* identifier = extract_var_decl(lhs)) {
                if (m_do_updates) {
                    if(!m_live_variables.is_live(identifier)) {
                        if (!m_source_loc_updated[binary_op->getSourceRange().getBegin().getRawEncoding()]) {
                            clang::Expr* rhs = binary_op->getRHS();
    
                            std::string replacement{};

                            // its possible that the rhs of a binary assignment operator
                            // is used by its parent expression. Hence we should be conservative
                            // about removing it. 
                            if (rhs) {
                                llvm::raw_string_ostream replacement_stream(replacement);
                                rhs->printPretty(replacement_stream, nullptr, m_policy);
                                replacement_stream.flush();
                            }

                            m_rewriter.ReplaceText(binary_op->getSourceRange(), replacement);
                            m_did_elim = true;
                            m_source_loc_updated[binary_op->getSourceRange().getBegin().getRawEncoding()] = true;
                        }
                    }
                }

                if (binary_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Assign) {
                    m_live_variables.mark_as_dead(identifier);
                } else {
                    m_live_variables.mark_as_live(identifier);
                }
                m_live_variables.record_usage(identifier);
                llvm::outs() << "Recording usage of " << identifier->getNameAsString() << "\n";
            }
        }
    }

    return true;
}

bool transferLivenessStmtVisitor::VisitImplicitCastExpr(clang::ImplicitCastExpr* ice) {
  if (ice->getCastKind() == clang::CK_LValueToRValue) {
    if (clang::DeclRefExpr* decl_ref_expr = llvm::dyn_cast<clang::DeclRefExpr>(ice->getSubExpr()->IgnoreParens())) {
        if(is_primitive_type(decl_ref_expr->getType())) {
            if (clang::VarDecl *var_decl = llvm::dyn_cast<clang::VarDecl>(decl_ref_expr->getDecl())) {
                m_live_variables.mark_as_live(var_decl);
                m_live_variables.record_usage(var_decl);
                llvm::outs() << "Recording usage of " << var_decl->getNameAsString() << "\n";
            }
        }
    }
  }
  return true;
}

bool transferLivenessStmtVisitor::VisitDeclStmt(clang::DeclStmt* decl_stmt) {
    for (clang::Decl* decl : decl_stmt->decls()) {
        if (clang::VarDecl* var_decl = llvm::dyn_cast<clang::VarDecl>(decl)) {
            recurse_through_type(var_decl->getType());
            if(is_primitive_type(var_decl->getType())) {
                if (m_do_updates) {
                    if(!m_live_variables.is_live(var_decl)) {
                        if (!m_source_loc_updated[decl_stmt->getSourceRange().getBegin().getRawEncoding()]) {
                            bool has_init = var_decl->hasInit();
                            clang::Expr* init = var_decl->getInit();
                            bool init_has_side_effects = has_init ? init->HasSideEffects(m_ctx, /* includePossibleSideEffects = */ true) : false;
                            bool var_has_usage = m_live_variables.has_usage(var_decl);
    
                            llvm::outs() << var_decl->getNameAsString() << " is to be eliminated and ";
                            if(var_has_usage) {
                                if (has_init && !init_has_side_effects) {
                                    llvm::outs() << "it has usage and retains init\n";
                                    clang::VarDecl *NewVD = clang::VarDecl::Create(
                                        m_ctx,
                                        const_cast<clang::DeclContext*>(var_decl->getDeclContext()),
                                        var_decl->getBeginLoc(),
                                        var_decl->getLocation(),
                                        var_decl->getIdentifier(),
                                        var_decl->getType(),
                                        m_ctx.getTrivialTypeSourceInfo(var_decl->getType().getUnqualifiedType()),
                                        var_decl->getStorageClass()
                                    );
                                    NewVD->setConstexpr(var_decl->isConstexpr());
                                    NewVD->setTSCSpec(var_decl->getTSCSpec());
                                    if(var_decl->isInlineSpecified())
                                        NewVD->setInlineSpecified();
    
                                    for (clang::Attr *A : var_decl->attrs())
                                        NewVD->addAttr(A->clone(m_ctx));
    
    
                                    std::string replacement{};
                                    llvm::raw_string_ostream replacement_stream(replacement);
                                    NewVD->print(replacement_stream, m_policy);
                                    replacement_stream << ";";
                                    replacement_stream.flush();
    
                                    m_rewriter.ReplaceText(decl_stmt->getSourceRange(), replacement);
                                    m_did_elim = true;
                                    m_source_loc_updated[decl_stmt->getSourceRange().getBegin().getRawEncoding()] = true;
                                } else {
                                    llvm::outs() << "it has usage but no init\n";
                                }
                            } else {
                                llvm::outs() << "does not have usage ";
                                std::string replacement{};
        
                                if (has_init && init_has_side_effects) {
                                    llvm::outs() << "and retains init\n";
                                    llvm::raw_string_ostream replacement_stream(replacement);
                                    init->printPretty(replacement_stream, nullptr, m_policy);
                                    replacement_stream << ";";
                                    replacement_stream.flush();
                                } else {
                                    llvm::outs() << "does not retain init\n";
                                }
        
                                m_rewriter.ReplaceText(decl_stmt->getSourceRange(), replacement);
                                m_did_elim = true;
                                m_source_loc_updated[decl_stmt->getSourceRange().getBegin().getRawEncoding()] = true;
                            }
                        }
                    }
                }

                m_live_variables.mark_as_dead(var_decl);
            }
        }
    }
    return true;
}

bool transferLivenessStmtVisitor::VisitUnaryOperator(clang::UnaryOperator* unary_op) {
    typedef clang::UnaryOperator::Opcode un_opcode;
    un_opcode unop = unary_op->getOpcode();
    /**
     * possible flavors => 
     *     1. post/pre inc/dec
     *     2. neg/not/lnot
     *     3. AddrOf/Deref
     *     4. [Not handled] co_await / complex / extension marker
     */
    // mark the target variable as live and record its usage
    if (unop >= un_opcode::UO_PostInc && unop <= un_opcode::UO_PreDec) {
        clang::Expr* inner_expr = unary_op->getSubExpr()->IgnoreParens();
        if (clang::VarDecl* identifier = extract_var_decl(inner_expr)) {
            if(is_primitive_type(identifier->getType())) {
                m_live_variables.mark_as_live(identifier);
                m_live_variables.record_usage(identifier);
            }
        }
    }

    // record the usage of the target variable. 
    if (unop == un_opcode::UO_AddrOf) {
        clang::Expr* inner_expr = unary_op->getSubExpr()->IgnoreParens();
        if (clang::VarDecl* identifier = extract_var_decl(inner_expr)) {
            if(is_primitive_type(identifier->getType())) {
                m_live_variables.record_usage(identifier);
            }
        }
    }

    if (unop == un_opcode::UO_Deref) {
        // the only case in which we should not mark aliased
        // and global variables as live is when the immediate
        // parent of this Deref operation is the binary
        // assignment operator (=) and this unary op
        // occurs in the lhs of such an op. 
        if (!is_parent_an_assignment_op(unary_op)) {
            mark_aliased_and_global_vars_as_live();
        }
    }

    return true;
}

bool transferLivenessStmtVisitor::VisitArraySubscriptExpr(clang::ArraySubscriptExpr* ase) {
    if (clang::VarDecl* identifier = extract_var_decl(ase->getBase()->IgnoreParenCasts())) {
        if(is_primitive_type(identifier->getType())) {
            m_live_variables.record_usage(identifier);
        }
    }
    if (!is_parent_an_assignment_op(ase)) {
        mark_aliased_and_global_vars_as_live();
    }
    return true;
}

bool transferLivenessStmtVisitor::VisitGCCAsmStmt(clang::GCCAsmStmt* asm_stmt) {
    for (unsigned i = 0; i < asm_stmt->getNumOutputs(); ++i) {
        if (clang::VarDecl* identifier = extract_var_decl(asm_stmt->getOutputExpr(i))) {
            if(is_primitive_type(identifier->getType())) {
                m_live_variables.record_usage(identifier);
            }
        }
    }

    return true;
}

bool transferLivenessStmtVisitor::is_parent_an_assignment_op(clang::Expr* expr) {
    const clang::Expr* current = expr;

    while (true) {
        auto parents = m_ctx.getParents(*current);
        if (parents.empty()) return false;

        const auto* parent_stmt = parents[0].get<clang::Stmt>();
        if (!parent_stmt) return false;

        // strip outer parens if any
        if (const auto* paren = llvm::dyn_cast<clang::ParenExpr>(parent_stmt)) {
            current = paren;
            continue;
        }

        if (const auto* bin_op = llvm::dyn_cast<clang::BinaryOperator>(parent_stmt)) {
            return (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Assign) && (bin_op->getLHS() == current);
        }

        return false;
    }
}

void transferLivenessStmtVisitor::mark_aliased_and_global_vars_as_live() {
    for(auto it : m_aliased_and_global_vars) {
        m_live_variables.mark_as_live(it.first);
    }
}

bool transferLivenessStmtVisitor::VisitCallExpr(clang::CallExpr* call) {
    mark_aliased_and_global_vars_as_live();

    return true;
}

bool transferLivenessStmtVisitor::TraverseStmtExpr(clang::StmtExpr* stmt_expr) {
    return true;
}

bool transferLivenessStmtVisitor::TraverseBinaryOperator(clang::BinaryOperator* bin_op) {
    if (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Comma) return true;
    else return clang::RecursiveASTVisitor<transferLivenessStmtVisitor>::TraverseBinaryOperator(bin_op);
}

// ─── AliasedAndGlobalVarCollector ──────────────────────────────────────────────

class AliasedAndGlobalVarCollector : public clang::RecursiveASTVisitor<AliasedAndGlobalVarCollector> {
private:
    std::unordered_map<clang::VarDecl*, bool> m_aliased_and_global_vars;
    void recurse_through_type(clang::QualType T) {
        const clang::Type *TP = T.getTypePtr();

        if (const auto *TOE = llvm::dyn_cast<clang::TypeOfExprType>(TP)) {
            if (clang::Expr *e = TOE->getUnderlyingExpr()) {
                this->TraverseStmt(e);
            }
            recurse_through_type(TOE->desugar());
            return;
        } else if (const auto *TOT = llvm::dyn_cast<clang::TypeOfType>(TP)) {
            recurse_through_type(TOT->desugar());
            return;
        }

        if (const auto *VAT = llvm::dyn_cast<clang::VariableArrayType>(TP)) {
            if (clang::Expr *size = VAT->getSizeExpr())
                this->TraverseStmt(size);
            recurse_through_type(VAT->getElementType());
        } else if (const auto *PT = llvm::dyn_cast<clang::PointerType>(TP)) {
            recurse_through_type(PT->getPointeeType());
        } else if (const auto *RT = llvm::dyn_cast<clang::ReferenceType>(TP)) {
            recurse_through_type(RT->getPointeeType());
        } else if (const auto *AT = llvm::dyn_cast<clang::ArrayType>(TP)) {
            recurse_through_type(AT->getElementType());
        } else if (const auto *FPT = llvm::dyn_cast<clang::FunctionProtoType>(TP)) {
            recurse_through_type(FPT->getReturnType());
            for (auto paramType : FPT->param_types())
                recurse_through_type(paramType);
        } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
            recurse_through_type(FT->getReturnType());
        } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
            recurse_through_type(FT->getReturnType());
        } else if (const auto *MPT = llvm::dyn_cast<clang::MemberPointerType>(TP)) {
            recurse_through_type(MPT->getPointeeType());
        } else if (const auto *VT = llvm::dyn_cast<clang::VectorType>(TP)) {
            recurse_through_type(VT->getElementType());
        } else if (const auto *ParenT = llvm::dyn_cast<clang::ParenType>(TP)) {
            recurse_through_type(ParenT->getInnerType());
        }
    }
public:
    bool VisitUnaryOperator(clang::UnaryOperator* unary_op) {
        if (unary_op->getOpcode() == clang::UO_AddrOf) {
            if (auto* decl_ref_expr = llvm::dyn_cast<clang::DeclRefExpr>(unary_op->getSubExpr()->IgnoreParens()))
                if (auto* var_decl = llvm::dyn_cast<clang::VarDecl>(decl_ref_expr->getDecl()))
                    m_aliased_and_global_vars[var_decl] = true;
        }

        return true;
    }

    bool VisitDeclStmt(clang::DeclStmt* decl_stmt) {
        for (clang::Decl* decl : decl_stmt->decls()) {
            if (clang::VarDecl* var_decl = llvm::dyn_cast<clang::VarDecl>(decl)) {
                recurse_through_type(var_decl->getType());
                if (var_decl->hasGlobalStorage()) {
                    m_aliased_and_global_vars[var_decl] = true;
                }
            }
        }

        return true;
    }

    bool VisitDeclRefExpr(clang::DeclRefExpr* decl_ref_expr) {
        if (clang::VarDecl *var_decl = llvm::dyn_cast<clang::VarDecl>(decl_ref_expr->getDecl())) {
            if (var_decl->hasGlobalStorage()) {
                m_aliased_and_global_vars[var_decl] = true;
            }
        }

        return true;
    }

    std::unordered_map<clang::VarDecl*, bool> get_aliased_and_global_var_set() {
        return std::move(m_aliased_and_global_vars);
    }
};

// ─── deadStoreElimVisitor ──────────────────────────────────────────────

deadStoreElimVisitor::deadStoreElimVisitor(clang::ASTContext& context, clang::Rewriter& rewriter)
    : m_ctx(context), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_did_elim(false) {}

bool deadStoreElimVisitor::did_elim() { return m_did_elim; }

void deadStoreElimVisitor::transfer(live_variables_t& live_vars, clang::Stmt* stmt, bool do_updates, std::unordered_map<unsigned, bool>& source_loc_updated, std::unordered_map<clang::VarDecl*, bool>& aliased_and_global_vars) {
    /**
     * TODO: The analysis is currently only for primtive types. 
     *       Extend it for Arrays and Records. 
     * 
     * In order to eliminate DeclStmts, we keep track of variables 
     * that were used somewhere regardless of whether they are dead
     * or alive when data-flow reaches the DeclStmt. If it turns out 
     * that the variables was not used at all, we can remove the VarDecl
     * else we can conditionally remove the init expression, provided
     * it did not have any side-effect. 
     * 
     * 1. VarDecl -> When we encounter a VarDecl, the lhs is obviously going
     *               to be marked dead. Now the thing of interest is when we
     *               are doing updates. Now if the lhs is live when we reach
     *               the declStmt, then we don't change anything. If its dead,
     *               then there are 4 scenarios. Depending on whether it has
     *               usage further down the line and whether its init has side
     *               effects or not. If it has usage and the init has side effects,
     *               then we don't change anything. If it has usage but init has 
     *               no side-effects, then we remove the init. If it does not have
     *               usage but init has side-effects, then we retain only the init.
     *               If it does not have usage and the init does not has side-effects,
     *               we remove the declStmt altogether. 
     * 2. BinaryOp              -> check lhs to mark VarDecl as live/dead
     *    (Assignment Variants)    if compound assignment operator, mark as live
     *                             otherwise mark it as dead. If the lhs is dead
     *                             when we encounter a binary_op, and its the second
     *                             iteration, then rewrite the binary operator. Remove
     *                             the lhs and the operator but retain the rhs if it has
     *                             any side-effects. 
     * 3. unaryOp -> check lvalue on which op happens and mark it as live. Most of 
     *               these should be caught by the DeclRefExpr (lvalue-to-rvalue)
     * 4. CallExpr -> mark variables that either have global storage
     *                or posses aliasing potential as live. 
     * 5. unaryOp -> Mark the variable as being used but do not mark it as live.
     *    (AddrOf)   The value stored in the variable is not used, only the address
     *               of the variable is required. That means we can potentially erase
     *               some dead stores further up the line. 
     * 6. unaryOp -> Mark all variables that can alias and variables with global storage
     *    (DeRef)    as live. If the immediate parent of this Deref is a an assignment op
     *               then don't mark those variables live and also do not kill any variable. 
     * 7. ArraySubscriptExpr -> Mark all variables that can alias and variables with global storage
     *                          as live. If the immediate parent of this Deref is a an assignment op
     *                          then don't mark those variables live and also do not kill any variable. 
     * [Note: For 6 and 7, if the ptr is to shift and its not an array, then its undefined]
     * [      behavior and the standard allows us to optimize it away however we see fit. ]
     * [      This intenionally incorrect behavior is observed in both gcc and clang      ]
     * [      For 7, we don't keep track of which pointer points to which variable and so ]
     * [      we have no way of knowing if the arraySubscriptOp was not used for a var ptr]
     * [      For 6 and 7 both, we are being conservative here. Derferencing a pointer    ]
     * [      and using the value stored in it shoudl all aliased variables / global vars ]
     * [      alive while storing values in it should not mark any variable as dead.      ]
     * 8. ImplicitCastExpr (lvalue-to-rvalue) +
     *           DeclRefExpr + VarDecl         -> these represent usage of a variable that 
     *                                            has storage in an expression. Proper usage
     *                                            would entail converting the lvalue to an 
     *                                            rvalue. Mark such variables as live. 
     */
    transferLivenessStmtVisitor visitor(m_ctx, m_rewriter, m_policy, live_vars, do_updates, m_did_elim, source_loc_updated, aliased_and_global_vars);
    visitor.TraverseStmt(stmt);
}

void deadStoreElimVisitor::meet(live_variables_t& merge_into, const live_variables_t& merge_from) {
    merge_into.merge(merge_from);
}

bool deadStoreElimVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody()) return true;
    if (!func->isThisDeclarationADefinition()) return true;

    std::unique_ptr<clang::CFG> mapped_cfg = clang::CFG::buildCFG(func, func->getBody(), &m_ctx, m_cfg_options);
    if (!mapped_cfg) return true;
    mapped_cfg->dump(m_ctx.getLangOpts(), true);
    llvm::outs() << "==============================================\n\n";

    std::deque<clang::CFGBlock*> work_queue;
    work_queue.push_back(&(mapped_cfg->getExit()));
    std::unordered_map<clang::CFGBlock*, live_variables_t> block_live_vars_map;
    std::unordered_map<unsigned, bool> source_loc_updated;

    for (clang::CFGBlock *_block : *mapped_cfg) {
        block_live_vars_map[_block] = {};
    }

    AliasedAndGlobalVarCollector visitor;
    visitor.TraverseStmt(func->getBody());
    std::unordered_map<clang::VarDecl*, bool> aliased_and_global_vars = visitor.get_aliased_and_global_var_set();

    // first stabilize the live_variables set for each basic block
    while (!work_queue.empty()) {
        clang::CFGBlock *_block = work_queue.front();
        work_queue.pop_front();

        // meet function merges the live variables
        // coming from the block's successors
        live_variables_t live_variables;
        if (_block == &(mapped_cfg->getExit())) {
            // preserve the init seed to prevent global 
            // vars from being marked dead. The exit block
            // should not be anyone's predecessor and so 
            // it should never be inserted back in the worklist
            // and thus this check should be entered only once. 
            for (auto it : aliased_and_global_vars) {
                if (it.first->hasGlobalStorage()) {
                    live_variables.mark_as_live(it.first);
                }
            }
        }
        for (auto succ : _block->succs()) {
            if (!succ) continue;
            meet(live_variables, block_live_vars_map[succ]);
        }

        // transfer function changes the live variables
        // to take use-kill in the current block into account
        for(auto it = _block->rbegin(); it != _block->rend(); it++) {
            if (auto cfg_stmt = it->getAs<clang::CFGStmt>()) {
                if (clang::Stmt* stmt = const_cast<clang::Stmt*>(cfg_stmt->getStmt())) {
                    // guard against certain node types that can contain complex SubStatements. 
                    // While clangCFG takes care of compoundStmt, the same is not true for 
                    // others. This is required else these subStatements will be run the
                    // transfer function in a forward analysis fashion which is just wrong. 
                    // The expressions / statements in such nodes appear further up in the 
                    // CFG Block and so we can just skip them. 
                    if (llvm::dyn_cast<clang::StmtExpr>(stmt)) continue;
                    if (clang::BinaryOperator* bin_op = llvm::dyn_cast<clang::BinaryOperator>(stmt); bin_op && (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Comma)) continue;
                    transfer(live_variables, stmt, false, source_loc_updated, aliased_and_global_vars);
                }
            }
        }
        if (clang::Stmt* stmt = _block->getTerminatorStmt()) {
            if (auto asm_stmt = llvm::dyn_cast<clang::GCCAsmStmt>(stmt)) {
                transfer(live_variables, asm_stmt, false, source_loc_updated, aliased_and_global_vars);
            }
        }
        live_variables.mark_is_not_bottom();

        bool live_variables_has_changed = live_variables != block_live_vars_map[_block];

        for (auto pred : _block->preds()) {
            if (!pred) continue;
            if (live_variables_has_changed) {
                if (std::find(work_queue.begin(), work_queue.end(), pred) == work_queue.end()) {
                    work_queue.push_back(pred);
                }
            }
        }

        if (live_variables_has_changed) 
            block_live_vars_map[_block] = live_variables;
    }

    llvm::outs() << "starting the second iteration\n";

    // once the live_variables are stabilized, run the algorithm again,
    // only this time, allow the transfer function to do updates
    work_queue.push_back(&(mapped_cfg->getExit()));
    std::unordered_map<clang::CFGBlock*, bool> block_visited;

    while (!work_queue.empty()) {
        clang::CFGBlock *_block = work_queue.front();
        work_queue.pop_front();

        // meet function merges the live variables
        // coming from the block's successors
        live_variables_t live_variables;
        if (_block == &(mapped_cfg->getExit())) {
            // preserve the init seed to prevent global 
            // vars from being marked dead. The exit block
            // should not be anyone's predecessor and so 
            // it should never be inserted back in the worklist
            // and thus this check should be entered only once. 
            for (auto it : aliased_and_global_vars) {
                if (it.first->hasGlobalStorage()) {
                    live_variables.mark_as_live(it.first);
                }
            }
        }
        for (auto succ : _block->succs()) {
            if (!succ) continue;
            meet(live_variables, block_live_vars_map[succ]);
        }

        // transfer function changes the live variables
        // to take use-kill in the current block into account
        for(auto it = _block->rbegin(); it != _block->rend(); it++) {
            if (auto cfg_stmt = it->getAs<clang::CFGStmt>()) {
                if (clang::Stmt* stmt = const_cast<clang::Stmt*>(cfg_stmt->getStmt())) {
                    // guard against certain node types that can contain complex SubStatements. 
                    // While clangCFG takes care of compoundStmt, the same is not true for 
                    // StmtExpr. This is required else these subStatements will be run the
                    // transfer function in a forward analysis fashion which is just wrong. 
                    // The expressions / statements in such nodes appear further up in the 
                    // CFG Block and so we can just skip them. 
                    if (llvm::dyn_cast<clang::StmtExpr>(stmt)) continue;
                    if (clang::BinaryOperator* bin_op = llvm::dyn_cast<clang::BinaryOperator>(stmt); bin_op && (bin_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Comma)) continue;
                    transfer(live_variables, stmt, true, source_loc_updated, aliased_and_global_vars);
                }
            }
        }
        if (clang::Stmt* stmt = _block->getTerminatorStmt()) {
            if (auto asm_stmt = llvm::dyn_cast<clang::GCCAsmStmt>(stmt)) {
                transfer(live_variables, asm_stmt, true, source_loc_updated, aliased_and_global_vars);
            }
        }
        live_variables.mark_is_not_bottom();
        assert(live_variables == block_live_vars_map[_block] && "live_variables set should not change when the transfer function is allowed to make changes");

        block_visited[_block] = true;
        for (auto pred : _block->preds()) {
            if (!pred) continue;
            // now we check whether the block has been visited or not
            // because transfer function is isolated to a block. Hence
            // the changes it is supposed to do should happen only once. 
            if (!block_visited[pred]) {
                if(std::find(work_queue.begin(), work_queue.end(), pred) == work_queue.end()) {
                    work_queue.push_back(pred);
                }
            }
        }
    }

    return true;
}

// ─── preprocess ────────────────────────────────────────────────────

std::string preprocess(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::string result = source;

    // Insert braces around single-statement control flow bodies.
    {
        static const auto style = []() {
            auto s = clang::format::getLLVMStyle();
            s.InsertBraces = true;
            s.ColumnLimit = 0;
            return s;
        }();
        auto replacements = clang::format::reformat(
            style, result,
            {clang::tooling::Range(0, result.size())});
        auto formatted =
            clang::tooling::applyAllReplacements(result, replacements);
        if (formatted)
            result = std::move(*formatted);
    }

    // Specialize Function templates
    // run conditionally if the compiler flags indicates we
    // have to treat the source code as C++
    // {
    //     clang::Rewriter rewriter;
    //     std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
    //         rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
    //         TemplateSpecializerVisitor visitor(ctx, rewriter, sema);
    //         visitor.TraverseDecl(ctx.getTranslationUnitDecl());
    //     }, rewriter);
    //     if(isCompilable(tmp_result, filename, args)) {
    //         result = std::move(tmp_result);
    //     } else {
    //         llvm::outs() << "Specialize Function Templates Pass Failed\n";
    //         return tmp_result;
    //     }
    // }

    // Suppress Clang's implicit builtin declarations so that hoisted
    // temporaries use the type names from the user's headers (e.g.
    // `size_t`) rather than Clang-internal typedefs (`__size_t`).
    args.emplace_back("-fno-builtin");
    // Suppress warnings that show up as errors while parsing using
    // libtooling but are otherwise benign as still compiles the TU. 
    // For eg. -Wincompatible-pointer-types
    args.emplace_back("-Wno-everything");

    // Gather function definitions and move them after all function
    // declarations and Canonicalize all local / global variables defined 
    // in a translation unit. 
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            // The trick here is to make the changes within the AST
            CanonicalizeVarDeclVisitor _visitor(ctx, rewriter);
            _visitor.TraverseDecl(ctx.getTranslationUnitDecl());

            DeclBeforeDefVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());

            auto& SM = ctx.getSourceManager();
            auto endOfFile = SM.getLocForEndOfFile(SM.getMainFileID());
            rewriter.InsertTextAfter(endOfFile, visitor.getDeferredDefinitions());
        }, rewriter);
        // Remove redefinitions of any record that might have been
        // introduced because of separating Multiple Declarators
        // onto separate statements.
        {
            clang::Rewriter rewriter;
            tmp_result = runInMemory(tmp_result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
                rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
                NormalizeMultipleRecordDeclVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }, rewriter);
        }
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Declarations Before Definitions Preprocessing failed\n";
            return tmp_result;
        }
    }

    // Marks the beginning of every case statement with a semi-colon so
    // that any function call present at the start of the set of statements
    // for case is hoisted as part of the case and not above it.
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            MarkSwitchCaseVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Switch-case Preprocessing failed\n";
            return tmp_result;
        }
    }

    // Breaks `else if` statements into `else { if  }` statements if 
    // the expr in `if(expr)` contains a function-call. 
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            BreakElseIfVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Else If Preprocessing failed\n";
            return tmp_result;
        }
    }

    // Wraps statements which may have a declStmt (for/if/switch) with a compound stmt
    // This helps provide a valid space to hoist a comma separated declarator list
    // and also valid space for any loop rewrite. 
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            CompoundStmtWrapperVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "CompoundStmtWrapperVisitor failed\n";
            return tmp_result;
        }
    }

    // Wraps binary operator (,) with a paranthesis. Because the binary operator
    // can occur without any wrapper as well which can lead to errors while inlining
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            ParenExprWrapperVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "ParenExprWrapperVisitor failed\n";
            return tmp_result;
        }
    }

    // Writes Loops in their IR form at the source code level. 
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            LoopToIRVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Loops Preprocessing failed\n";
            return tmp_result;
        }
    }

    // Splits comma-separated variable declarations into individual statements
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            CommaInitVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        // Remove redefinitions of any record that might have been
        // introduced because of separating Multiple Declarators
        // onto separate statements.
        {
            clang::Rewriter rewriter;
            tmp_result = runInMemory(tmp_result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
                rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
                NormalizeMultipleRecordDeclVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }, rewriter);
        }
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "CommaInitVisitor failed\n";
            return tmp_result;
        }
    }

    size_t current_index;

    /// rewrite short-circuit operations into their IR form at the source code level
    bool didConvertToIR = true;
    current_index = 0;
    while (didConvertToIR) {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &current_index, &didConvertToIR](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            ShortCircuitOperatorToIRVisitor visitor(ctx, rewriter, current_index);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            didConvertToIR = visitor.didConvertToIR();
        }, rewriter);
        if(!isCompilable(tmp_result, filename, args)) {
            llvm::outs() << "Short-circuit Preprocessing failed\n";
            return tmp_result;
        }
        result = std::move(tmp_result);
    }

    // constant fold mathematical expressions into their simplified form
    bool did_fold = true;
    while (did_fold) {
        did_fold = false;
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &did_fold](clang::ASTContext& ctx, clang::Sema&) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            constantFoldVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            did_fold = visitor.did_fold();
        }, rewriter);
        if(!isCompilable(tmp_result, filename, args)) {
            llvm::outs() << "Constant Folding Preprocessing failed\n";
            return tmp_result;
        }
        result = std::move(tmp_result);
    }

    // Hoist nested function calls (repeat until stable).
    bool did_cleanup = true;
    current_index = 0;
    while (did_cleanup) {
        did_cleanup = false;
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &did_cleanup, &current_index](clang::ASTContext& ctx, clang::Sema&) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            FuncCallCleanupVisitor visitor(ctx, rewriter, current_index);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            did_cleanup = visitor.did_cleanup();
        }, rewriter);
        if(!isCompilable(tmp_result, filename, args)) {
            llvm::outs() << "Function Hoisting Preprocessing failed\n";
            return tmp_result;
        }
        result = std::move(tmp_result);
    }

    return result;
}

// ─── separate_functions ────────────────────────────────────────────

std::string separate_functions(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::string result;

    clang::Rewriter rewriter;
    runInMemory(source, filename, args, [&rewriter, &result](clang::ASTContext& ctx, clang::Sema&) {
        rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        FilterVisitor visitor(ctx, rewriter);
        visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        result = visitor.toJSON();
    }, rewriter);

    return result;
}

// ─── inline_functions ──────────────────────────────────────────────

void updateInlineLocInfo(std::vector<size_t>& m_call_sites, size_t m_current_index, size_t m_net_lines) {
    for (size_t i = m_current_index + 1; i < m_call_sites.size(); i++) {
        if (m_call_sites[i] >= m_call_sites[m_current_index]) {
                m_call_sites[i] += m_net_lines;
        }
    }
}

InlineResult inline_functions(const std::string& source, const std::string& filename,
                              const std::vector<InlineEntry>& entries, std::vector<std::string>& args) {
    // Suppress Clang's implicit builtin declarations so that hoisted
    // temporaries use the type names from the user's headers (e.g.
    // `size_t`) rather than Clang-internal typedefs (`__size_t`).
    args.emplace_back("-fno-builtin");
    // Suppress warnings that show up as errors while parsing using
    // libtooling but are otherwise benign as still compiles the TU. 
    // For eg. -Wincompatible-pointer-types
    args.emplace_back("-Wno-everything");
    
    std::vector<std::pair<std::string, std::string>> oracle;
    std::vector<size_t> call_sites;

    for (const auto& entry : entries) {
        oracle.emplace_back(entry.caller, entry.callee);
        call_sites.emplace_back(entry.line);
    }

    std::string result = source;
    std::vector<bool> succeeded(oracle.size(), false);

    for (size_t i = 0; i < oracle.size(); i++) {
        // opt records does contain information to inline destructors. 
        // They however do not show up in the ast and thus can't be matched 
        // using libtooling. Thus we instead manually identify a destructor
        if((oracle[i].second)[0] == '~') {
            succeeded[i] = true;
            continue;
        }
        clang::Rewriter rewriter;
        size_t net_lines;
        std::string tmp_result = runInMemory(
            result, filename, args,
            [&rewriter, &oracle, &call_sites, &succeeded, &i, &net_lines](clang::ASTContext& ctx, clang::Sema&) {
                rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
                FunctionDeclMapVisitor _visitor(ctx, rewriter);
                _visitor.TraverseDecl(ctx.getTranslationUnitDecl());

                InlineVisitor visitor(ctx, oracle[i].first, oracle[i].second, call_sites, i, rewriter, _visitor.get_function_decl_map());
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                if(!visitor.matched())
                    llvm::outs() << "could not find any matching pair for this: " << oracle[i].first << " " << oracle[i].second << " " << call_sites[i] << "\n";
                succeeded[i] = visitor.matched();
                net_lines = visitor.get_net_change();
            },
            rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
            // Adjust line numbers for subsequent call sites in the same file
            // that shifted due to inserted code.
            updateInlineLocInfo(call_sites, i, net_lines);
        } else {
            llvm::outs() << "Inlined failed for -> " << i << "\n";
            succeeded[i] = false;
        }
        llvm::outs() << "Done with Number -> " << i << "\n";
    }

    return {result, succeeded};
}

// ─── constant fold ────────────────────────────────────────────

std::string constant_fold(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change, bool* is_compilable) {
    std::string result = source;

    // constant fold mathematical expressions into their simplified form
    bool did_fold = true;
    while (did_fold) {
        did_fold = false;
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &did_fold](clang::ASTContext& ctx, clang::Sema&) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            constantFoldVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            did_fold = visitor.did_fold();
        }, rewriter);
        if(!isCompilable(tmp_result, filename, args)) {
            is_compilable ? (*is_compilable = false) : false;
            break;
        }
        result = std::move(tmp_result);

        did_change ? (*did_change ? true : (*did_change = did_fold)) : false;
    }

    return result;
}

// ─── dead-code-elimination ────────────────────────────────────────────

std::string deadCodeElim(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change, bool* is_compilable) {
    std::string result = source;

    // eliminates dead code in a way that transforms the structure of these code constructs
    bool did_elim = true;
    while (did_elim) {
        did_elim = false;
        clang::Rewriter rewriter;
        std::string tmp_result;
        
        bool had_structural_changes = true;
        bool had_bad_changes = false;
        while (had_structural_changes) {
            clang::Rewriter rewriter;
            had_structural_changes = false;
            tmp_result = runInMemory(result, filename, args, [&rewriter, &had_structural_changes](clang::ASTContext& ctx, clang::Sema&) {
                rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
                deadCodeElimVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                had_structural_changes = visitor.did_elim();
            }, rewriter);
            if(!isCompilable(tmp_result, filename, args)) {
                is_compilable ? (*is_compilable = false) : false;
                result = std::move(tmp_result);
                had_bad_changes = true;
                break;
            }
            result = std::move(tmp_result);
            did_elim = did_elim ? true : had_structural_changes;
        }

        if(had_bad_changes) break;

        // cfg analysis
        rewriter = clang::Rewriter();
        tmp_result = runInMemory(result, filename, args, [&rewriter, &did_elim](clang::ASTContext& ctx, clang::Sema&) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            deadCodeElimCfgAnalysisVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            did_elim = did_elim ? true : visitor.did_elim();
        }, rewriter);
        if (!isCompilable(tmp_result, filename, args)) {
            is_compilable ? (*is_compilable = false) : false;
            result = std::move(tmp_result);
            break;
        }
        result = std::move(tmp_result);

        did_change ? (*did_change ? true : (*did_change = did_elim)) : false;
    }

    return result;
}

// ─── copy-propagation ────────────────────────────────────────────

std::string copyPropagation(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change, bool* is_compilable) {
    std::string result = source;

    // propagates copies as when copies are seen. 
    bool did_elim = true;
    while (did_elim) {
        did_elim = false;

        // cfg analysis
        clang::Rewriter rewriter = clang::Rewriter();
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &did_elim](clang::ASTContext& ctx, clang::Sema&) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            copyPropagationVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            did_elim = visitor.did_propagate();
        }, rewriter);
        if (!isCompilable(tmp_result, filename, args)) {
            is_compilable ? (*is_compilable = false) : false;
            break;
        }
        result = std::move(tmp_result);

        did_change ? (*did_change ? true : (*did_change = did_elim)) : false;
    }

    return result;
}

// ─── dead-store-elimination ────────────────────────────────────────────

std::string deadStoreElim(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change, bool* is_compilable) {
    std::string result = source;

    bool did_elim = true;
    while (did_elim) {
        did_elim = false;
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &did_elim](clang::ASTContext& ctx, clang::Sema&) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            deadStoreElimVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            did_elim = visitor.did_elim();
        }, rewriter);
        if (!isCompilable(tmp_result, filename, args)) {
            is_compilable ? (*is_compilable = false) : false;
            break;
        }
        result = std::move(tmp_result);
        
        did_change ? (*did_change ? true : (*did_change = did_elim)) : false;
    }

    return result;
}

// ---- one-for-all ------------------------------------------------------------
std::string oneForAll(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::string result = source;

    bool did_change = true;
    int num_passes = 0;
    while (did_change) {
        did_change = false;
        bool is_compilable = true;
        result = constant_fold(result, filename, args, &did_change, &is_compilable);
        if(!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY CONSTANT-FOLD\n";
            return result;
        }
        result = deadCodeElim(result, filename, args, &did_change, &is_compilable);
        if(!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY DEAD-CODE-ELIMINATION\n";
            return result;
        }
        result = copyPropagation(result, filename, args, &did_change, &is_compilable);
        if(!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY COPY-PROPAGATION\n";
            return result;
        }
        result = deadCodeElim(result, filename, args, &did_change, &is_compilable);
        if (!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY DEAD-CODE-ELIMINATION\n";
            return result;
        }
        result = deadStoreElim(result, filename, args, &did_change, &is_compilable);
        if(!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY DEAD-STORE-ELIMINATION\n";
            return result;
        }
        llvm::outs() << "[OPTIMIZE]: Done with iteration> " << ++num_passes << "\n";
    }

    llvm::outs() << "[OPTIMIZE]: CONVERGED AT ITERATION> " << num_passes << "\n";
    return result;
}

}  // namespace invisible

/*
export UV_CONCURRENT_DOWNLOADS=4
export UV_CONCURRENT_INSTALLS=4
export UV_BUILD_JOBS=2
export UV_INDEX_PRIVATE_REGISTRY_USERNAME=aws
export UV_INDEX_PRIVATE_REGISTRY_PASSWORD=$(aws codeartifact get-authorization-token --domain reveng --domain-owner 127214196211 --query authorizationToken --output text --profile compiler-engineer-codeartifact --region eu-west-2)
ulimit -v $((8 * 1024 * 1024))
taskset -c 0-7 uv pip install -e . --no-cache --reinstall
*/
