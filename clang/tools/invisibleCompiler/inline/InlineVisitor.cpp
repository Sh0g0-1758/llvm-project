#include "inline/InlineVisitor.h"
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

    // a return leaves the inlined body, through the label after it
    if (auto* return_stmt = llvm::dyn_cast<clang::ReturnStmt>(stmt)) {
        std::string exit = "goto __inline_exit_" + std::to_string(m_current_index) + ";";
        clang::Expr* ret_val = return_stmt->getRetValue();
        if (!ret_val) {
            stream << exit << "\n";
            return true;
        }
        stream << "{ ";
        if (!m_target_var.empty())
            stream << m_target_var << " = ";
        ret_val->printPretty(stream, this, m_policy);
        stream << "; " << exit << " }\n";
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

bool InlineVisitor::isTransparentParent(const clang::Stmt* s, bool isExplicitCoversionOperator) {
    return llvm::isa<clang::ParenExpr,
                     clang::ImplicitCastExpr,
                     clang::ExprWithCleanups,
                     clang::CXXBindTemporaryExpr,
                     clang::MaterializeTemporaryExpr>(s) || 
           (isExplicitCoversionOperator ? 
            (llvm::isa<clang::ExplicitCastExpr>(s)) : false);
}

clang::DynTypedNode InlineVisitor::getSemanticParent(const clang::Stmt* node, bool isExplicitCoversionOperator) {
    for (const clang::Stmt* child = node;;) {
        auto parents = m_ctx.getParents(*child);
        if (parents.empty()) return {};
        const auto* parent = parents[0].get<clang::Stmt>();
        if (!parent || !isTransparentParent(parent, isExplicitCoversionOperator)) return parents[0];
        child = parent;
    }
}

bool InlineVisitor::isTopLevelCall(const clang::CallExpr* call) {
    bool isExplicitCoversionOperator = false;
    if (auto* member_call_expr = llvm::dyn_cast<clang::CXXMemberCallExpr>(call)) {
        const auto* conversion_decl = llvm::dyn_cast_or_null<clang::CXXConversionDecl>(member_call_expr->getMethodDecl());
        if (conversion_decl && conversion_decl->isExplicit()) {
            isExplicitCoversionOperator = true;
        }
    }

    auto parent_node = getSemanticParent(call, isExplicitCoversionOperator);
    if (parent_node.getNodeKind().isNone())
        return false;

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
        // a = foo();  where a is a class type
        } else if (const auto* op_call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(parent_stmt)) {
            if (op_call->getOperator() == clang::OO_Equal) {
                auto oc_parents = m_ctx.getParents(*op_call);
                if (!oc_parents.empty()) {
                    if (const auto* grandparent = oc_parents[0].get<clang::Stmt>()) {
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
      m_names(ctx),
      m_function_decl_map(decl_map) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
}

// functions are named by their mangled names, which the oracle gives
bool InlineVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    m_current_func = m_names.getName(func);
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

    if (!llvm::is_contained(m_names.getAllManglings(ctor), m_callee))
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

    if (!callee || !callee->hasBody())
        return true;

    // This check is required in case the function hoisting pass failed. Because then we can
    // have foo(bar()) and we can match with foo instead.
    if (m_callee != m_names.getName(callee))
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

        // a va_list that is an array is passed as a pointer, which va_arg doesn't
        // take, so the argument itself is used
        clang::QualType va_list_type = m_ctx.getBuiltinVaListType();
        if (va_list_type->isArrayType() && m_ctx.hasSameType(param_decl->getOriginalType(), va_list_type)) {
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

    // the body is looked up where it is inlined, so its names are qualified, and
    // its types printed canonically, with their scopes
    clang::PrintingPolicy policy = m_policy;
    if (m_ctx.getLangOpts().CPlusPlus) {
        clang::Stmt* body = callee->getBody();
        qualifyNonLocalNames(body, m_ctx);
        policy.PrintAsCanonical = true;
    }
    InlinePrinterHelper helper(m_ctx, target_var, caller_params, policy, m_current_index);
    callee->getBody()->printPretty(stream, &helper, policy);
    stream.flush();

    // Trim leading/trailing blank lines.
    size_t first_content = raw_body.find_first_not_of(" \n\r\t");
    size_t last_content = raw_body.find_last_not_of(" \n\r\t");
    if (first_content != std::string::npos && last_content != std::string::npos)
        raw_body = raw_body.substr(first_content, last_content - first_content + 1);

    OS << raw_body << "\n__inline_exit_" << m_current_index << ": ;";
    added_lines += std::count(raw_body.begin(), raw_body.end(), '\n') + 1;

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
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_names(ctx) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
}

bool FunctionDeclMapVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody()) return true;
    if (!func->isThisDeclarationADefinition()) return true;

    m_function_decl_map[m_names.getName(func)] = func;
    return true;
}

std::map<std::string, clang::FunctionDecl*>& FunctionDeclMapVisitor::get_function_decl_map()  {
    return m_function_decl_map;
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
    addPipelineFlags(args);
    
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
                    llvm::errs() << "invisible: warning: no call from '" << oracle[i].first << "' to '" << oracle[i].second << "' at line " << call_sites[i] << "\n";
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
            llvm::errs() << "invisible: warning: inlining '" << oracle[i].second << "' into '" << oracle[i].first << "' at line " << call_sites[i]
                         << " produced code that does not compile, skipped it\n";
            verbose() << lastCompileFailure().diagnostics;
            succeeded[i] = false;
        }
        verbose() << "done with oracle entry " << i << "\n";
    }

    return {result, succeeded};
}

}  // namespace invisible
