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

namespace invisible {

// ─── DeclBeforeDefVisitor ──────────────────────────────────────────────

DeclBeforeDefVisitor::DeclBeforeDefVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
}

bool DeclBeforeDefVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody()) return true;
    if (!func->isThisDeclarationADefinition()) return true;

    std::string def;
    llvm::raw_string_ostream def_out(def);
    func->print(def_out, m_policy);
    def_out.flush();
    m_deferred_definitions.push_back(def);

    clang::PrintingPolicy declPolicy = m_policy;
    declPolicy.TerseOutput = true;
    std::string decl;
    llvm::raw_string_ostream decl_out(decl);
    func->print(decl_out, declPolicy);
    decl_out.flush();
    decl += ";";

    m_rewriter.ReplaceText(func->getSourceRange(), decl);
    return true;
}

std::string DeclBeforeDefVisitor::getDeferredDefinitions() {
    std::string all_defs;
    size_t total = 1;
    for (const auto& def : m_deferred_definitions)
        total += def.size() + 2;
    all_defs.reserve(total);

    all_defs += '\n';
    for (auto& def : m_deferred_definitions) {
        all_defs += std::move(def);
        all_defs += "\n\n";
    }
    return all_defs;
}

// ─── ShortCircuitOperatorToIRVisitor ──────────────────────────────────────────────

ShortCircuitOperatorToIRVisitor::ShortCircuitOperatorToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, size_t& current_index)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_current_index(current_index), m_last_update_loc(0) {
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
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

        m_rewriter.InsertTextBefore(nearest_compound_stmt, ir_form);
        m_rewriter.ReplaceText(binary_op->getSourceRange(), short_circuit_result);
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

        m_rewriter.InsertTextBefore(nearest_compound_stmt, ir_form);
        m_rewriter.ReplaceText(binary_op->getSourceRange(), short_circuit_result);
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

    std::string short_circuit_result = "__short_circuit_result_" + std::to_string(m_current_index++);
    clang::QualType resType = clang::TypeName::getFullyQualifiedType(conditional_op->getLHS()->getType(), m_ctx, false);

    resType.print(out, m_policy, short_circuit_result);
    out << ";\n";
    out << "if (" << cond << ") {\n";
    out << short_circuit_result << " = " << lexpr << ";\n";
    out << "} else {\n";
    out << short_circuit_result << " = " << rexpr << ";\n";
    out << "}\n";
    out.flush();

    m_rewriter.InsertTextBefore(nearest_compound_stmt, ir_form);
    m_rewriter.ReplaceText(conditional_op->getSourceRange(), short_circuit_result);

    return true;
}

clang::SourceLocation ShortCircuitOperatorToIRVisitor::findInsertionLoc(const clang::ConditionalOperator* conditional_op) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*conditional_op);
    const clang::Stmt* last_stmt = nullptr;

    while (true) {
        auto parents = m_ctx.getParents(current);
        if (parents.empty()) {
            return {};
        }

        const auto& parent = parents[0];
        if (parent.get<clang::CompoundStmt>() != nullptr) {
            if (last_stmt == nullptr) {
                return {};
            }
            return last_stmt->getBeginLoc();
        }
        if (const auto* stmt = parent.get<clang::Stmt>()) {
            last_stmt = stmt;
        }

        current = parent;
    }
}

clang::SourceLocation ShortCircuitOperatorToIRVisitor::findInsertionLoc(const clang::BinaryOperator* binary_op) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*binary_op);
    const clang::Stmt* last_stmt = nullptr;

    while (true) {
        auto parents = m_ctx.getParents(current);
        if (parents.empty()) {
            return {};
        }

        const auto& parent = parents[0];
        if (parent.get<clang::CompoundStmt>() != nullptr) {
            if (last_stmt == nullptr) {
                return {};
            }
            return last_stmt->getBeginLoc();
        }
        if (const auto* stmt = parent.get<clang::Stmt>()) {
            last_stmt = stmt;
        }

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
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
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
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
}

bool MarkSwitchCaseVisitor::VisitCaseStmt(clang::CaseStmt* case_stmt) {
    m_rewriter.InsertTextBefore(case_stmt->getSubStmt()->getSourceRange().getBegin(), ";\n");
    return true;
}

// ─── LoopToIRVisitor ──────────────────────────────────────────────

LoopToIRVisitor::LoopToIRVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
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

// ─── TemplateSpecializerVisitor ──────────────────────────────────────────────

TemplateSpecializerVisitor::TemplateSpecializerVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, clang::Sema& sema)
    : m_ctx(ctx), m_rewriter(rewriter), m_sema(sema) {
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
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
    : m_ctx(ctx), m_rewriter(rewriter), m_is_uninstantiated_template(false), m_current_index(current_index) {
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
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

    // If the callee's defintion is not present in the current TU
    if(!(call->getDirectCallee())) return true;

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

    auto& src_mgr = m_ctx.getSourceManager();
    const auto& lang_opts = m_ctx.getLangOpts();

    auto call_text =
        clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(call->getSourceRange()), src_mgr, lang_opts)
            .str();

    std::string ret_var{};
    std::string clean_call{};
    llvm::raw_string_ostream out(clean_call);

    if (!call->getType()->isVoidType()) {
        clang::QualType retTy = call->getDirectCallee()->getReturnType();
        clang::QualType fqTy =
            clang::TypeName::getFullyQualifiedType(retTy, m_ctx, false);

        clang::PrintingPolicy policy(m_ctx.getLangOpts());
        if (m_ctx.getLangOpts().CPlusPlus)
            policy.adjustForCPlusPlus();

        ret_var = "__func_hoist_ret__" + std::to_string(m_current_index++);
        fqTy.print(out, policy, ret_var);
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
        llvm::outs() << "Begin Loc : " << call->getSourceRange().getBegin().getRawEncoding() << " End Loc : " << call->getSourceRange().getEnd().getRawEncoding() << "\n";
        m_rewriter.ReplaceText(call->getSourceRange(), ret_var);
    }
    m_last_update_loc = nearest_compound_stmt;
}

bool FuncCallCleanupVisitor::VisitDeclStmt(clang::DeclStmt* decl_stmt) {
    if (decl_stmt->isSingleDecl())
        return true;

    for (const clang::Decl* decl : decl_stmt->decls()) {
        if (const auto* var_decl = llvm::dyn_cast<clang::VarDecl>(decl)) {
            if(var_decl->hasInit()) {
                if(auto* call_expr = llvm::dyn_cast<clang::CallExpr>(var_decl->getInit())) {
                    hoist_call_at_nearest_compound_statement(const_cast<clang::CallExpr*>(call_expr));
                    return true;
                }
            }
        } else {
            return true;
        }
    }

    return true;
}

clang::SourceLocation FuncCallCleanupVisitor::findInsertionLoc(const clang::CallExpr* call) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*call);
    const clang::Stmt* last_stmt = nullptr;

    while (true) {
        auto parents = m_ctx.getParents(current);
        if (parents.empty()) {
            return {};
        }

        const auto& parent = parents[0];
        if (parent.get<clang::CompoundStmt>() != nullptr) {
            if (last_stmt == nullptr) {
                return {};
            }
            return last_stmt->getBeginLoc();
        }
        if (const auto* stmt = parent.get<clang::Stmt>()) {
            last_stmt = stmt;
        }

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
            return true;
        }
    }
    return false;
}

// ─── FilterVisitor ─────────────────────────────────────────────────

FilterVisitor::FilterVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter) : m_ctx(ctx), m_rewriter(rewriter) {
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
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
      }

void InlinePrinterHelper::renameVLASizeVars(clang::QualType T) {
    const clang::Type *TP = T.getCanonicalType().getTypePtr();

    if (const auto *VAT = llvm::dyn_cast<clang::VariableArrayType>(TP)) {
        if (clang::Expr *size = VAT->getSizeExpr())
            m_func_param_renamer.TraverseStmt(size);
        renameVLASizeVars(VAT->getElementType());
    } else if (const auto *DSAT = llvm::dyn_cast<clang::DependentSizedArrayType>(TP)) {
        if (clang::Expr *size = DSAT->getSizeExpr())
            m_func_param_renamer.TraverseStmt(size);
        renameVLASizeVars(DSAT->getElementType());
    } else if (const auto *PT = llvm::dyn_cast<clang::PointerType>(TP)) {
        renameVLASizeVars(PT->getPointeeType());
    } else if (const auto *RT = llvm::dyn_cast<clang::ReferenceType>(TP)) {
        renameVLASizeVars(RT->getPointeeType());
    } else if (const auto *AT = llvm::dyn_cast<clang::ArrayType>(TP)) {
        renameVLASizeVars(AT->getElementType());
    } else if (const auto *FPT = llvm::dyn_cast<clang::FunctionProtoType>(TP)) {
        renameVLASizeVars(FPT->getReturnType());
        for (auto paramType : FPT->param_types())
            renameVLASizeVars(paramType);
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        renameVLASizeVars(FT->getReturnType());
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        renameVLASizeVars(FT->getReturnType());
    } else if (const auto *MPT = llvm::dyn_cast<clang::MemberPointerType>(TP)) {
        renameVLASizeVars(MPT->getPointeeType());
    } else if (const auto *VT = llvm::dyn_cast<clang::VectorType>(TP)) {
        renameVLASizeVars(VT->getElementType());
    } else if (const auto *ParenT = llvm::dyn_cast<clang::ParenType>(TP)) {
        renameVLASizeVars(ParenT->getInnerType());
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
                // arr[n] -> arr[__inline_x_y]
                renameVLASizeVars(var_decl->getType());
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

    if (auto* label_stmt = llvm::dyn_cast<clang::LabelStmt>(stmt)) {
        std::string label = label_stmt->getDecl()->getName().str();
        if(m_goto_labels.find(label) == m_goto_labels.end()) {
            m_goto_labels[label] = std::string("__inline_label_") + std::to_string(m_current_index) + "__" + std::to_string(m_goto_label_index++);   
        }
        stream << m_goto_labels.at(label) << ":\n";
        label_stmt->getSubStmt()->printPretty(stream, this, m_policy);
        stream << ";";
        return true;
    }

    if (auto* goto_stmt = llvm::dyn_cast<clang::GotoStmt>(stmt)) {
        std::string label = goto_stmt->getLabel()->getName().str();
        if(m_goto_labels.find(label) == m_goto_labels.end()) {
            m_goto_labels[label] = std::string("__inline_label_") + std::to_string(m_current_index++) + "__" + std::to_string(m_goto_label_index++);
        }
        stream << "goto ";
        stream << m_goto_labels.at(label);
        stream << ";\n";
        return true;
    }

    // Clang's StmtPrinter always emits "asm" for GCCAsmStmt, but in GNU C
    // mode only "__asm__" is valid.  Print the statement ourselves.
    if (auto* gcc_asm = llvm::dyn_cast<clang::GCCAsmStmt>(stmt)) {
        stream << "__asm__";
        if (gcc_asm->isVolatile()) {
            stream << " volatile";
        }
        if (gcc_asm->isAsmGoto()) {
            stream << " goto";
        }
        stream << "(";
        gcc_asm->getAsmStringExpr()->printPretty(stream, this, m_policy);

        unsigned nOutputs = gcc_asm->getNumOutputs();
        unsigned nInputs = gcc_asm->getNumInputs();
        unsigned nClobbers = gcc_asm->getNumClobbers();

        if (nOutputs != 0 || nInputs != 0 || nClobbers != 0) {
            stream << " : ";
            for (unsigned i = 0; i < nOutputs; ++i) {
                if (i != 0) {
                    stream << ", ";
                }
                llvm::StringRef name = gcc_asm->getOutputName(i);
                if (!name.empty()) {
                    stream << "[" << name << "] ";
                }
                stream << "\"" << gcc_asm->getOutputConstraint(i) << "\" (";
                gcc_asm->getOutputExpr(i)->printPretty(stream, this, m_policy);
                stream << ")";
            }
        }
        if (nInputs != 0 || nClobbers != 0) {
            stream << " : ";
            for (unsigned i = 0; i < nInputs; ++i) {
                if (i != 0) {
                    stream << ", ";
                }
                llvm::StringRef name = gcc_asm->getInputName(i);
                if (!name.empty()) {
                    stream << "[" << name << "] ";
                }
                stream << "\"" << gcc_asm->getInputConstraint(i) << "\" (";
                gcc_asm->getInputExpr(i)->printPretty(stream, this, m_policy);
                stream << ")";
            }
        }
        if (nClobbers != 0) {
            stream << " : ";
            for (unsigned i = 0; i < nClobbers; ++i) {
                if (i != 0) {
                    stream << ", ";
                }
                stream << "\"" << gcc_asm->getClobber(i) << "\"";
            }
        }
        stream << ");\n";
        return true;
    }

    if (auto* return_stmt = llvm::dyn_cast<clang::ReturnStmt>(stmt)) {
        if (!m_target_var.empty()) {
            stream << "*" << m_target_var << " = ";
            if (clang::Expr* ret_val = return_stmt->getRetValue())
                ret_val->printPretty(stream, this, m_policy);
            stream << ";\n";
        }
        return true;
    }

    return false;
}

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
                             clang::Rewriter& rewriter)
    : m_ctx(ctx),
      m_caller(caller),
      m_callee(callee),
      m_call_sites(call_sites),
      m_current_index(current_index),
      m_rewriter(rewriter),
      m_policy(m_ctx.getLangOpts()) {
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
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

    clang::FunctionDecl* callee = call->getDirectCallee();
    // the callee might be a templated function which in turn
    // is being called by a template function. Thus at the callsite
    // the body is not available. The callee can also be a virtual function
    if (!callee || !callee->hasBody())
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

    std::string dummy_inline_result_pointer{};
    if(!target_var.empty()) {
        dummy_inline_result_pointer = "__inline_result__" + std::to_string(m_current_index);
        clang::QualType retTy = callee->getReturnType();
        clang::QualType fqTy = clang::TypeName::getFullyQualifiedType(retTy, m_ctx, false);
        clang::QualType ptr_type = m_ctx.getPointerType(fqTy);

        ptr_type.print(OS, m_policy, dummy_inline_result_pointer);
        OS << " = &" << target_var << ";\n";
        added_lines += 1;
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

    InlinePrinterHelper helper(m_ctx, dummy_inline_result_pointer, caller_params, m_policy, m_current_index);
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
    auto orig_text = clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(stmt_to_replace->getSourceRange()), src_mgr, m_ctx.getLangOpts());
    m_net_lines = added_lines - std::count(orig_text.begin(), orig_text.end(), '\n');

    OS << "}";
    OS.flush();
    m_rewriter.ReplaceText(stmt_to_replace->getSourceRange(), inlined_code);
    return true;
}

bool InlineVisitor::matched() const { return m_matched; }

size_t InlineVisitor::get_net_change() { return m_net_lines; }

// ─── ConstantFoldVisitor ──────────────────────────────────────────────

constantFoldVisitor::constantFoldVisitor(clang::ASTContext& context, clang::Rewriter& rewriter) : m_ctx(context), m_rewriter(rewriter) {
    rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
}

bool constantFoldVisitor::did_fold() {
    return (m_last_update_loc_begin.getRawEncoding() != 0) && (m_last_update_loc_end.getRawEncoding() != 0);
}

bool constantFoldVisitor::VisitExpr(clang::Expr* expr) {
    if (expr->getSourceRange().isInvalid()) return true;
    if ((expr->getSourceRange().getBegin() >= m_last_update_loc_begin) && (expr->getSourceRange().getEnd() <= m_last_update_loc_end)) return true;

    // ── DEBUG ────────────────────────────────────────────────────────────────
    llvm::errs() << "\n=== [constantFold] Checking AST of expression to fold ===\n";
    auto& src_mgr = m_ctx.getSourceManager();
    auto& lang_opts = m_ctx.getLangOpts();

    auto expr_text = clang::Lexer::getSourceText(
        clang::CharSourceRange::getTokenRange(expr->getSourceRange()),
        src_mgr, lang_opts
    ).str();

    llvm::errs() << "Source text: " << expr_text << "\n";
    // ─────────────────────────────────────────────────────────────────────────

    const clang::Expr* inner = expr->IgnoreImpCasts();
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

    clang::Expr::EvalResult result;
    clang::Expr::EvalResult inner_result;
    if (expr->isValueDependent() || inner->isValueDependent() || !expr->EvaluateAsRValue(result, m_ctx) || !inner->EvaluateAsRValue(inner_result, m_ctx))
        return true;

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
    llvm::errs() << "\n=== [constantFold] AST of expression being folded ===\n";
    expr->dump();                                          // full AST subtree
    // ─────────────────────────────────────────────────────────────────────────

    m_last_update_loc_begin = expr->getSourceRange().getBegin();
    m_last_update_loc_end = expr->getSourceRange().getEnd();
    m_rewriter.ReplaceText(
        clang::CharSourceRange::getTokenRange(expr->getSourceRange()),
        replacement);

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
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            TemplateSpecializerVisitor visitor(ctx, rewriter, sema);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        }
    }

    // Gather function definitions and move them after all function
    // declarations. 
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            DeclBeforeDefVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());

            auto& SM = ctx.getSourceManager();
            auto endOfFile = SM.getLocForEndOfFile(SM.getMainFileID());
            rewriter.InsertTextAfter(endOfFile, visitor.getDeferredDefinitions());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Declarations Before Definitions Preprocessing failed\n";
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
            break;
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
            break;
        }
        result = std::move(tmp_result);
    }

    return result;
}

// ─── constant fold ────────────────────────────────────────────

std::string constant_fold(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
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
            break;
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
                InlineVisitor visitor(ctx, oracle[i].first, oracle[i].second, call_sites, i, rewriter);
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
