#include "copy_propagation/CopyPropagationVisitor.h"
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
        // Verify that the identifier we are copying has a 
        // valid type. It should already be a type that we handle
        // but can still have qualifiers that we do not support. 
        if (var_decl != expr_identifier && is_primitive_type(expr_identifier->getType())) {
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
                            if(std::string to_copy = m_copies.find_if_copy_exists(var_decl, m_ctx, m_policy); !to_copy.empty()) {
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
                            if(std::string to_copy = m_copies.find_if_copy_exists(var_decl, m_ctx, m_policy); !to_copy.empty()) {
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
                if(std::string to_copy = m_copies.find_if_copy_exists(var_decl, m_ctx, m_policy); !to_copy.empty()) {
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
        } else if (is_dereference_operation(inner_expr)) {
            m_copies.kill_copies_with_global_storage();
            m_copies.kill_copies_that_can_alias();
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
            if(std::string to_copy = m_copies.find_if_copy_exists(var_decl, m_ctx, m_policy); !to_copy.empty()) {
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
    clang::ASTContext& m_ctx;
public:
    varsInScopeCollector(clang::ASTContext& context) : m_ctx(context) {}

    void getDeclFromParent(clang::CompoundStmt* stmt, bool& is_first_decl_stmt, clang::VarDecl*& anchor) {
        auto parents = m_ctx.getParents(*stmt);
        if (parents.empty()) return;

        const auto& parent = parents[0];
        if (clang::ForStmt* for_stmt = const_cast<clang::ForStmt*>(parent.get<clang::ForStmt>())) {
            if(for_stmt->getInit()) {
                if (auto decl_stmt = llvm::dyn_cast<clang::DeclStmt>(for_stmt->getInit())) {
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
        }
    }    

    bool VisitCompoundStmt(clang::CompoundStmt* compound_stmt) {
        bool is_first_decl_stmt = true;
        clang::VarDecl* anchor;
        getDeclFromParent(compound_stmt, is_first_decl_stmt, anchor);

        for (clang::Stmt* child : compound_stmt->body()) {
            if(clang::LabelStmt* label_stmt = llvm::dyn_cast<clang::LabelStmt>(child))
                child = label_stmt->getSubStmt();

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
    varsInScopeCollector visitor(m_ctx);
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

}  // namespace invisible
