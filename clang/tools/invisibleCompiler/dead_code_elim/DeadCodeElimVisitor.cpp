#include "dead_code_elim/DeadCodeElimVisitor.h"
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
        DeclBeforeDefPrinterHelper m_custom_printer(m_ctx, m_policy);
        if_stmt->getThen()->printPretty(reduced_stream, &m_custom_printer, m_policy);
    } else {
        if (clang::Stmt* else_stmt = if_stmt->getElse()) {
            DeclBeforeDefPrinterHelper m_custom_printer(m_ctx, m_policy);
            else_stmt->printPretty(reduced_stream, &m_custom_printer, m_policy);
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

        if (for_stmt->getInit()) {
            for_stmt->getInit()->printPretty(reduced_stream, nullptr, m_policy);
            reduced_stream << ";";
        }

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
    
            DeclBeforeDefPrinterHelper m_custom_printer(m_ctx, m_policy);
            do_stmt->getBody()->printPretty(reduced_stream, &m_custom_printer, m_policy);
    
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


    std::unordered_map<clang::CFGBlock*, size_t> block_retain_decl;
    // record control flow block that introduces variables which might be
    // used later on in other control flow blocks that are live. This behavior is 
    // present when goto expressions mess up the program flow. 
    for(auto block_status : reachable_block) {
        block_retain_decl[block_status.first] = 0;
        if (!block_status.second) {
            clang::CFGBlock* _block = block_status.first;

            /**
             * The intuition here is that we want to see if any variable that was introduced
             * in this block escapes the block. That is possible if we don't see a conforming
             * scopeEnd for the variable. 
             * 
             * eg.
             * 
             * ```
             * int foo() {
             *    goto label;
             *    int a;
             *    if (a > 56) {
             *  label:
             *      a = 90;
             *      return a;
             *    }
             *  }
             * ```
             * The basic block with `int a` declaration will not have a CFGSCopeEnd Statement 
             * but it is used in a reachable block. Hence when we are cleaning up the unreachable
             * block that contains a, we should preserve its declaration.
             * 
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

            if (scope_state.size()) {
                block_retain_decl[block_status.first] = 0;
                // scope state is > 0.
                // Possibilities are : 
                // a. Begin Decl (a)
                // b. Decl Begin Decl (a) | (Begin b)
                // A Begin state should always precede a Decl state. 
                // so counting the number of decls in the scope state
                // should tell us how many layers of the declaration
                // we need to preserve. 
                while(!scope_state.empty()) {
                    if (scope_state.top() == DECL) {
                        block_retain_decl[block_status.first]++;
                    }
                    scope_state.pop();
                }
            }
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
            int decl_depth_to_retain = block_retain_decl[_block];
            for(auto Elem = _block->begin(); Elem < _block->end(); Elem++) {
                if(terminator_has_label_stmt) {
                    if (Elem == (_block->end() - 1)) continue;
                }
                if (auto cfg_stmt = Elem->getAs<clang::CFGStmt>()) {
                    if (clang::Stmt* stmt = const_cast<clang::Stmt*>(cfg_stmt->getStmt())) {
                        // selectively retain some statements that are essential to
                        // conform to the C grammar. Here we retain declarations for 
                        // variables that are used in reachable blocks further down
                        // the line. Strip their init though. 
                        if (decl_depth_to_retain >= 0) {
                            if (clang::DeclStmt* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(stmt)) {
                                for (clang::Decl* decl : decl_stmt->decls()) {
                                    if (clang::VarDecl* var_decl = llvm::dyn_cast<clang::VarDecl>(decl)) {
                                        if (var_decl->hasInit()) {
                                            unreachable_locs.push_back({var_decl->getInit()->getSourceRange().getBegin(), var_decl->getInit()->getSourceRange().getEnd()});

                                            if (var_decl->getInitStyle() != clang::VarDecl::CInit)
                                                continue;
    
                                            clang::SourceLocation init_start = var_decl->getInit()->getBeginLoc();
    
                                            auto prev_token = clang::Lexer::findPreviousToken(
                                                init_start, m_ctx.getSourceManager(), m_ctx.getLangOpts(), /*IncludeComments=*/false);
    
                                            if (prev_token && prev_token->is(clang::tok::equal)) {
                                                unreachable_locs.push_back({prev_token->getLocation(), prev_token->getLocation()});
                                            }
                                        }
                                    }
                                }
                                continue;
                            }
                        }
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
                } else if (auto scope_begin = Elem->getAs<clang::CFGScopeBegin>()) {
                    decl_depth_to_retain--;
                } else if (auto scope_end = Elem->getAs<clang::CFGScopeEnd>()) {
                    decl_depth_to_retain++;
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

// ─── deadCodeElimCleanupVisitor ──────────────────────────────────────────────

deadCodeElimCleanupVisitor::deadCodeElimCleanupVisitor(clang::ASTContext& context, clang::Rewriter& rewriter) : m_ctx(context), m_rewriter(rewriter), did_remove(false) { }

bool deadCodeElimCleanupVisitor::did_elim() {
    return did_remove;
}

bool deadCodeElimCleanupVisitor::is_parent_valid_to_remove_body(clang::CompoundStmt* stmt) {
    auto parents = m_ctx.getParents(*stmt);
    if (parents.empty()) return false;

    const auto& parent = parents[0];
    return parent.get<clang::CompoundStmt>() != nullptr;
}

bool deadCodeElimCleanupVisitor::is_parent_stmtExpr(clang::CompoundStmt* stmt) {
    auto parents = m_ctx.getParents(*stmt);
    if (parents.empty()) return false;

    const auto& parent = parents[0];
    return parent.get<clang::StmtExpr>() != nullptr;
}

bool deadCodeElimCleanupVisitor::VisitCompoundStmt(clang::CompoundStmt* stmt) {
    if(stmt->body_empty()) {
        if (!is_parent_valid_to_remove_body(stmt)) return true;

        m_removable_locs.push_back({stmt->getSourceRange().getBegin(), stmt->getSourceRange().getEnd()});
    }

    for(int i = 0; i < stmt->size(); i++) {
        if(i == (stmt->size() - 1) and is_parent_stmtExpr(stmt)) continue;
        clang::Stmt* child = (stmt->body_begin())[i];

        if (clang::NullStmt* null_stmt = llvm::dyn_cast<clang::NullStmt>(child)) {
            m_removable_locs.push_back({null_stmt->getSourceRange().getBegin(), null_stmt->getSourceRange().getEnd()});
        } else if (clang::Expr* expr_stmt = llvm::dyn_cast<clang::Expr>(child)) {
            if(!expr_stmt->HasSideEffects(m_ctx, /* includePossibleSideEffects = */ true)) {
                m_removable_locs.push_back({expr_stmt->getSourceRange().getBegin(), expr_stmt->getSourceRange().getEnd()});
            }
        }
    }

    return true;
}

void deadCodeElimCleanupVisitor::merge_and_clean_removable_locs() {
    if(m_removable_locs.empty()) return;
    did_remove = true;

    std::sort(m_removable_locs.begin(), m_removable_locs.end(), [](std::pair<clang::SourceLocation, clang::SourceLocation> a, std::pair<clang::SourceLocation, clang::SourceLocation> b) {
        return a.first.getRawEncoding() < b.first.getRawEncoding();
    });

    std::vector<std::pair<clang::SourceLocation, clang::SourceLocation>> merged_removable_locs;

    clang::SourceLocation rewrite_min_removable_loc = m_removable_locs[0].first;
    unsigned min_removable_loc = m_removable_locs[0].first.getRawEncoding();

    clang::SourceLocation rewrite_max_removable_loc = m_removable_locs[0].second;
    unsigned max_removable_loc = m_removable_locs[0].second.getRawEncoding();

    for(int i = 1; i < m_removable_locs.size(); i++) {
        unsigned start = m_removable_locs[i].first.getRawEncoding();
        unsigned end = m_removable_locs[i].second.getRawEncoding();

        // overlap if the start lies in between the max and min removable_loc
        if((min_removable_loc <= start) and (start <= max_removable_loc)) {
            max_removable_loc = std::max(end, max_removable_loc);
            if (end == max_removable_loc)
                rewrite_max_removable_loc = m_removable_locs[i].second;
        } else {
            merged_removable_locs.push_back({rewrite_min_removable_loc, rewrite_max_removable_loc});
            min_removable_loc = start;
            max_removable_loc = end;
            rewrite_min_removable_loc = m_removable_locs[i].first;
            rewrite_max_removable_loc = m_removable_locs[i].second;
        }
    }
    merged_removable_locs.push_back({rewrite_min_removable_loc, rewrite_max_removable_loc});

    for(auto& removable_loc : merged_removable_locs)
        m_rewriter.RemoveText({removable_loc.first, removable_loc.second});
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

        // cleanup
        bool did_cleanup = true;
        had_bad_changes = false;
        while(did_cleanup) {
            clang::Rewriter rewriter;
            did_cleanup = false;
            tmp_result = runInMemory(result, filename, args, [&rewriter, &did_cleanup](clang::ASTContext& ctx, clang::Sema&) {
                rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
                deadCodeElimCleanupVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                visitor.merge_and_clean_removable_locs();
                did_cleanup = visitor.did_elim();
            }, rewriter);
            if (!isCompilable(tmp_result, filename, args)) {
                is_compilable ? (*is_compilable = false) : false;
                result = std::move(tmp_result);
                had_bad_changes = true;
                break;
            }
            result = std::move(tmp_result);
            did_elim = did_elim ? true : did_cleanup;
        }

        if(had_bad_changes) break;

        did_change ? (*did_change ? true : (*did_change = did_elim)) : false;
    }

    return result;
}

}  // namespace invisible
