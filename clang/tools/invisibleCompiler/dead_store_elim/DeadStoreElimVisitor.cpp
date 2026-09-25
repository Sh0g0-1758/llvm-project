#include "dead_store_elim/DeadStoreElimVisitor.h"
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
                    if (binary_op->getOpcode() == clang::BinaryOperator::Opcode::BO_Assign) {
                        if(!m_live_variables.is_live(identifier)) {
                            if (!m_source_loc_updated[binary_op->getSourceRange().getBegin().getRawEncoding()]) {
                                clang::Expr* rhs = binary_op->getRHS();
        
                                std::string replacement{};

                                // its possible that the rhs of a binary assignment operator
                                // is used by its parent expression. Hence we should be conservative
                                // about removing it. 
                                if (rhs) {
                                    llvm::raw_string_ostream replacement_stream(replacement);
                                    if (llvm::dyn_cast<clang::ImplicitCastExpr>(rhs)) {
                                        replacement_stream << "(" << rhs->getType().getAsString(m_policy) << ")";
                                    }
                                    rhs->printPretty(replacement_stream, nullptr, m_policy);
                                    replacement_stream.flush();
                                }

                                m_rewriter.ReplaceText(binary_op->getSourceRange(), replacement);
                                m_did_elim = true;
                                m_source_loc_updated[binary_op->getSourceRange().getBegin().getRawEncoding()] = true;
                            }
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
                                llvm::raw_string_ostream replacement_stream(replacement);
        
                                if (has_init && init_has_side_effects) {
                                    llvm::outs() << "and retains init\n";
                                    init->printPretty(replacement_stream, nullptr, m_policy);
                                } else {
                                    llvm::outs() << "does not retain init\n";
                                }

                                replacement_stream << ";";
                                replacement_stream.flush();
        
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


    // when the input expression has a memory constraint, the variable
    // is not converted from an lvalue to an rvalue and thus not caught
    // recursively. The same is not troublesome for copy-propagation
    // because when we go recursively to the input expressions there
    // because there is no lvalue-to-rvalue cast, the said variable
    // is not changed which is the correct behavior. This behavior is conservative
    // but protects us from replacing an input operand with a memory constraint
    // with a constant which would make the TU unparsable. 
    for (unsigned i = 0; i < asm_stmt->getNumInputs(); ++i) {
        if (clang::VarDecl* identifier = extract_var_decl(asm_stmt->getInputExpr(i))) {
            if(is_primitive_type(identifier->getType())) {
                m_live_variables.record_usage(identifier);
                m_live_variables.mark_as_live(identifier);
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

        // check if a successor has been visited atleast once or not. 
        // Certain control flow graphs exist where certain nodes are 
        // reachable from the start but not from the exit block. A good
        // example is infinite loops. These blocks may use variables which 
        // if not taken into account might lead to deleting their declarations
        // and thus result in an invalid transformation. 
        for (auto succ : _block->succs()) {
            if (!succ) continue;

            if (block_live_vars_map[_block].is_bottom()) {
                if (std::find(work_queue.begin(), work_queue.end(), succ) == work_queue.end()) {
                    work_queue.push_back(succ);
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

        for (auto succ : _block->succs()) {
            if (!succ) continue;

            if (!block_visited[succ]) {
                if (std::find(work_queue.begin(), work_queue.end(), succ) == work_queue.end()) {
                    work_queue.push_back(succ);
                }
            }
        }
    }

    return true;
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

}  // namespace invisible
