#include "invisible.h"
#include "helpers.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/JSON.h"

#include <string>

namespace invisible {

// ─── CommaInitVisitor ──────────────────────────────────────────────

CommaInitVisitor::CommaInitVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) {
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
}

bool CommaInitVisitor::VisitDeclStmt(clang::DeclStmt* decl_stmt) {
    if (decl_stmt->isSingleDecl())
        return true;

    llvm::SmallVector<const clang::VarDecl*, 8> vars;
    for (const clang::Decl* decl : decl_stmt->decls()) {
        if (const auto* var_decl = llvm::dyn_cast<clang::VarDecl>(decl))
            vars.push_back(var_decl);
        else
            return true;
    }

    clang::PrintingPolicy policy(m_ctx.getLangOpts());
    policy.adjustForCPlusPlus();

    std::string replacement{};
    llvm::raw_string_ostream OS(replacement);

    for (const clang::VarDecl* var_decl : vars) {
        var_decl->print(OS, policy);
        OS << ";\n";
    }

    OS.flush();
    m_rewriter.ReplaceText(clang::CharSourceRange::getTokenRange(decl_stmt->getSourceRange()), replacement);
    return true;
}

// ─── FuncCallCleanupVisitor ────────────────────────────────────────

FuncCallCleanupVisitor::FuncCallCleanupVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter, bool& did_cleanup)
    : m_ctx(ctx), m_rewriter(rewriter), m_did_cleanup(did_cleanup) {
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
    m_did_cleanup = false;
}

bool FuncCallCleanupVisitor::VisitCallExpr(clang::CallExpr* call) {
    if(m_did_cleanup) return true;

    clang::FunctionDecl* callee = call->getDirectCallee();
    if (isTopLevelCall(call)) return true;

    m_did_cleanup = true;

    auto& src_mgr = m_ctx.getSourceManager();
    auto& lang_opts = m_ctx.getLangOpts();

    auto call_text =
        clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(call->getSourceRange()), src_mgr, lang_opts)
            .str();
    
    std::string ret_var{};
    std::string clean_call{};
    llvm::raw_string_ostream OS(clean_call);

    if(!call->getType()->isVoidType()) {
        clang::QualType retTy = callee->getReturnType();

        clang::PrintingPolicy policy(m_ctx.getLangOpts());
        policy.adjustForCPlusPlus();
        auto loc = src_mgr.getSpellingLoc(call->getExprLoc());
        unsigned spelling_line = src_mgr.getSpellingLineNumber(loc);
        unsigned spelling_col = src_mgr.getSpellingColumnNumber(loc);

        ret_var = callee->getNameAsString() + "_" + std::to_string(callee->getID()) + "_" +
                          std::to_string(spelling_line) + "_" + std::to_string(spelling_col) + "_Ret";
        retTy.print(OS, policy, ret_var);
        OS << " = ";
    }
    OS << call_text << ";\n";
    OS.flush();

    m_rewriter.InsertTextBefore(findInsertionLoc(call), clean_call);
    m_rewriter.ReplaceText(call->getSourceRange(), ret_var);
    return true;
}

clang::SourceLocation FuncCallCleanupVisitor::findInsertionLoc(const clang::CallExpr* call) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*call);
    const clang::Stmt* last_stmt = nullptr;

    while (true) {
        auto parents = m_ctx.getParents(current);
        if (parents.empty())
            return {};

        const auto& parent = parents[0];
        if (parent.get<clang::CompoundStmt>()) {
            if (!last_stmt)
                return {};
            return last_stmt->getBeginLoc();
        }
        if (const auto* stmt = parent.get<clang::Stmt>())
            last_stmt = stmt;

        current = parent;
    }
}

bool FuncCallCleanupVisitor::isTopLevelCall(const clang::CallExpr* call) {
    auto parents = m_ctx.getParents(*call);
    if (parents.empty())
        return false;

    const auto& parent_node = parents[0];

    // int x = foo();
    if (const auto* var_decl = parent_node.get<clang::VarDecl>()) {
        auto vd_parents = m_ctx.getParents(*var_decl);
        if (!vd_parents.empty()) {
            if (const auto* decl_stmt = vd_parents[0].get<clang::DeclStmt>()) {
                auto ds_parents = m_ctx.getParents(*decl_stmt);
                if (!ds_parents.empty()) {
                    if (const auto* parent_stmt = ds_parents[0].get<clang::Stmt>()) {
                        if (llvm::isa<clang::CompoundStmt>(parent_stmt))
                            return true;
                    }
                }
            }
        }
    } else if (const auto* parent_stmt = parent_node.get<clang::Stmt>()) {
        // x = foo();
        if (const auto* bin_op = llvm::dyn_cast<clang::BinaryOperator>(parent_stmt)) {
            if (bin_op->isAssignmentOp()) {
                auto bo_parents = m_ctx.getParents(*bin_op);
                if (!bo_parents.empty()) {
                    if (const auto* grandparent = bo_parents[0].get<clang::Stmt>()) {
                        if (llvm::isa<clang::CompoundStmt>(grandparent))
                            return true;
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

// ─── InlinePrinterHelper ───────────────────────────────────────────

InlinePrinterHelper::InlinePrinterHelper(clang::ASTContext& ctx, std::string prefix, std::string target_var,
                                         std::map<int, std::string> caller_params, clang::PrintingPolicy policy)
    : m_ctx(ctx),
      m_prefix(std::move(prefix)),
      m_target_var(std::move(target_var)),
      m_caller_params(std::move(caller_params)),
      m_policy(policy) {}

struct VLARenamer : clang::RecursiveASTVisitor<VLARenamer> {
    const std::string &Prefix;
    clang::ASTContext &Ctx;
    std::vector<std::pair<clang::VarDecl*, clang::IdentifierInfo*>> &renamed;
    std::map<int, std::string> &callerParams;

    VLARenamer(const std::string &P, clang::ASTContext &C,
            std::vector<std::pair<clang::VarDecl*, clang::IdentifierInfo*>> &R, std::map<int, std::string> &PR)
        : Prefix(P), Ctx(C), renamed(R), callerParams(PR) {}

    bool VisitDeclRefExpr(clang::DeclRefExpr *DRE) {
        if (clang::VarDecl *VD = llvm::dyn_cast<clang::VarDecl>(DRE->getDecl())) {
            std::string newName{};
            if (clang::ParmVarDecl *PVD = llvm::dyn_cast<clang::ParmVarDecl>(VD)) {
                unsigned idx = PVD->getFunctionScopeIndex();
                newName = callerParams[idx];
            } else {
                if(!VD->isLocalVarDecl()) newName = VD->getNameAsString();
                else newName = Prefix + VD->getNameAsString() + "_" + std::to_string(VD->getID());
            }
            clang::IdentifierInfo *oldII = VD->getIdentifier();
            renamed.push_back({VD, oldII});
            VD->setDeclName(clang::DeclarationName(&Ctx.Idents.get(newName)));
        }
        return true;
    }
};

void InlinePrinterHelper::renameVLASizeVars(clang::QualType T, clang::ASTContext &Ctx,
                    std::vector<std::pair<clang::VarDecl*, clang::IdentifierInfo*>> &renamed) {
    const clang::Type *TP = T.getCanonicalType().getTypePtr();

    if (const auto *VAT = llvm::dyn_cast<clang::VariableArrayType>(TP)) {
        if (clang::Expr *size = VAT->getSizeExpr()) {
            VLARenamer r(m_prefix, Ctx, renamed, m_caller_params);
            r.TraverseStmt(size);
        }
        renameVLASizeVars(VAT->getElementType(), Ctx, renamed);
    } else if (const auto *DSAT = llvm::dyn_cast<clang::DependentSizedArrayType>(TP)) {
        if (clang::Expr *size = DSAT->getSizeExpr()) {
            VLARenamer r(m_prefix, Ctx, renamed, m_caller_params);
            r.TraverseStmt(size);
        }
        renameVLASizeVars(DSAT->getElementType(), Ctx, renamed);
    } else if (const auto *PT = llvm::dyn_cast<clang::PointerType>(TP)) {
        renameVLASizeVars(PT->getPointeeType(), Ctx, renamed);
    } else if (const auto *RT = llvm::dyn_cast<clang::ReferenceType>(TP)) {
        renameVLASizeVars(RT->getPointeeType(), Ctx, renamed);
    } else if (const auto *AT = llvm::dyn_cast<clang::ArrayType>(TP)) {
        renameVLASizeVars(AT->getElementType(), Ctx, renamed);
    } else if (const auto *FPT = llvm::dyn_cast<clang::FunctionProtoType>(TP)) {
        renameVLASizeVars(FPT->getReturnType(), Ctx, renamed);
        for (auto paramType : FPT->param_types())
            renameVLASizeVars(paramType, Ctx, renamed);
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        renameVLASizeVars(FT->getReturnType(), Ctx, renamed);
    } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
        renameVLASizeVars(FT->getReturnType(), Ctx, renamed);
    } else if (const auto *MPT = llvm::dyn_cast<clang::MemberPointerType>(TP)) {
        renameVLASizeVars(MPT->getPointeeType(), Ctx, renamed);
    } else if (const auto *VT = llvm::dyn_cast<clang::VectorType>(TP)) {
        renameVLASizeVars(VT->getElementType(), Ctx, renamed);
    } else if (const auto *ParenT = llvm::dyn_cast<clang::ParenType>(TP)) {
        renameVLASizeVars(ParenT->getInnerType(), Ctx, renamed);
    }
}

bool InlinePrinterHelper::handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) {
    if (auto* decl_ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt)) {
        if (auto* var_decl = llvm::dyn_cast<clang::VarDecl>(decl_ref->getDecl())) {
            if (auto* param_decl = llvm::dyn_cast<clang::ParmVarDecl>(var_decl)) {
                stream << m_caller_params[param_decl->getFunctionScopeIndex()];
            } else {
                if (!var_decl->isLocalVarDecl()) stream << var_decl->getNameAsString();
                else stream << m_prefix << var_decl->getNameAsString() << "_" << var_decl->getID();
            }
            return true;
        }
    }

    if (auto* decl_stmt = llvm::dyn_cast<clang::DeclStmt>(stmt)) {
        for (auto it = decl_stmt->decl_begin(); it != decl_stmt->decl_end(); ++it) {
            if (auto* var_decl = llvm::dyn_cast<clang::VarDecl>(*it)) {
                std::string new_name = m_prefix + var_decl->getNameAsString() + "_" + std::to_string(var_decl->getID());

                clang::IdentifierInfo *oldII = var_decl->getIdentifier();
                var_decl->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(new_name)));

                std::vector<std::pair<clang::VarDecl*, clang::IdentifierInfo*>> sizeRenames;
                renameVLASizeVars(var_decl->getType(), m_ctx, sizeRenames);

                clang::Expr *init = var_decl->getInit();
                var_decl->setInit(nullptr);

                var_decl->print(stream, m_policy);

                var_decl->setInit(init);
                var_decl->setDeclName(clang::DeclarationName(oldII));
                for (auto &[var_decl, ii] : sizeRenames)
                    var_decl->setDeclName(clang::DeclarationName(ii));

                if (init) {
                    switch (var_decl->getInitStyle()) {
                        case clang::VarDecl::CInit:
                            stream << " = ";
                            init->printPretty(stream, this, m_policy);
                            stream << ";\n";
                            break;
                        case clang::VarDecl::CallInit:
                            if (auto *CCE = llvm::dyn_cast<clang::CXXConstructExpr>(init); CCE && CCE->getNumArgs() == 0) {
                                stream << ";\n";
                            } else {
                                stream << "(";
                                init->printPretty(stream, this, m_policy);
                                stream << ");\n";
                            }
                            break;
                        case clang::VarDecl::ListInit:
                            init->printPretty(stream, this, m_policy);
                            stream << ";\n";
                            break;
                        case clang::VarDecl::ParenListInit:
                            stream  << "(";
                            init->printPretty(stream, this, m_policy);
                            stream << ");\n";
                            break;
                    }
                } else {
                    stream << ";\n";
                }
            }
        }
        return true;
    }

    if (auto* return_stmt = llvm::dyn_cast<clang::ReturnStmt>(stmt)) {
        if (!m_target_var.empty()) {
            stream << m_target_var << " = ";
            if (clang::Expr* ret_val = return_stmt->getRetValue())
                ret_val->printPretty(stream, this, m_policy);
            stream << ";\n";
        }
        return true;
    }

    return false;
}

// ─── InlineVisitor ─────────────────────────────────────────────────

InlineVisitor::InlineVisitor(clang::ASTContext& ctx, const std::string& caller, const std::string& callee,
                             std::vector<std::pair<size_t, std::string>>& call_sites, size_t current_index,
                             clang::Rewriter& rewriter)
    : m_ctx(ctx),
      m_caller(caller),
      m_callee(callee),
      m_call_sites(call_sites),
      m_current_index(current_index),
      m_rewriter(rewriter) {
    m_rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
}

bool InlineVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    m_current_func = func->getNameInfo().getName().getAsString();
    return true;
}

bool InlineVisitor::VisitCallExpr(clang::CallExpr* call) {
    if (m_caller != m_current_func)
        return true;

    clang::FunctionDecl* callee = call->getDirectCallee();
    if (!callee || !callee->hasBody())
        return true;

    // TODO: Handle class methods — they may use private members
    // that need special treatment.
    if (llvm::isa<clang::CXXMethodDecl>(callee))
        return true;

    if (m_callee != callee->getNameAsString())
        return true;

    auto& ctx = callee->getASTContext();
    auto& src_mgr = ctx.getSourceManager();
    auto loc = src_mgr.getSpellingLoc(call->getExprLoc());
    auto ploc = src_mgr.getPresumedLoc(loc);

    if (ploc.getLine() != m_call_sites[m_current_index].first)
        return true;

    auto& lang_opts = m_ctx.getLangOpts();
    clang::PrintingPolicy Policy = m_ctx.getPrintingPolicy();
    Policy.adjustForCPlusPlus();
    Policy.Indentation = 2;

    std::string target_var{};
    auto parents = ctx.getParents(*call);
    const clang::Stmt* stmt_to_replace = call;

    std::string inlined_code{};
    llvm::raw_string_ostream OS(inlined_code);
    size_t added_lines = 0;

    if (!parents.empty()) {
        // xvalue cleanup
        if (const auto* expr_cleanup = parents[0].get<clang::ExprWithCleanups>())
            parents = ctx.getParents(*expr_cleanup);

        const auto& parent_node = parents[0];

        // int x = foo();
        if (const auto* var_decl = parent_node.get<clang::VarDecl>()) {
            clang::VarDecl *NewVD = clang::VarDecl::Create(
                m_ctx,
                const_cast<clang::DeclContext*>(var_decl->getDeclContext()),
                var_decl->getBeginLoc(),
                var_decl->getLocation(),
                var_decl->getIdentifier(),
                var_decl->getType(),
                m_ctx.getTrivialTypeSourceInfo(var_decl->getType()),
                var_decl->getStorageClass()
            );
            NewVD->setConstexpr(var_decl->isConstexpr());
            NewVD->setTSCSpec(var_decl->getTSCSpec());
            if(var_decl->isInlineSpecified())
                NewVD->setInlineSpecified();

            for (clang::Attr *A : var_decl->attrs())
                NewVD->addAttr(A->clone(m_ctx));
            NewVD->print(OS, Policy);
            OS << ";\n";
            added_lines += 1;
            target_var = var_decl->getNameAsString();

            auto grandparents = ctx.getParents(*var_decl);
            if (!grandparents.empty()) {
                if (const auto* grandparent_decl_stmt = grandparents[0].get<clang::DeclStmt>())
                    stmt_to_replace = grandparent_decl_stmt;
            }
        }
        // x = foo();
        else if (const auto* parent_stmt = parent_node.get<clang::Stmt>()) {
            if (const auto* bin_op = llvm::dyn_cast<clang::BinaryOperator>(parent_stmt)) {
                if (bin_op->isAssignmentOp()) {
                    target_var =
                        clang::Lexer::getSourceText(
                            clang::CharSourceRange::getTokenRange(bin_op->getLHS()->getSourceRange()), src_mgr,
                            lang_opts)
                            .str();
                    stmt_to_replace = bin_op;
                }
            }
        }
    }

    std::string callee_name = callee->getNameAsString();
    // Strip the operator suffix (+/-/*) from operator overloads
    if (llvm::dyn_cast<clang::CXXOperatorCallExpr>(call))
        callee_name = callee_name.substr(0, callee_name.size() - 2);

    std::string prefix =
        callee_name + "_" + std::to_string(callee->getID()) + "_" + std::to_string(m_current_index) + "_" + std::to_string(ploc.getLine()) + "_" + std::to_string(ploc.getColumn()) + "_";

    std::map<int, std::string> caller_params;
    for (unsigned i = 0; i < call->getNumArgs(); ++i) {
        clang::ParmVarDecl* param_decl = callee->getParamDecl(i);
        clang::Expr* arg = call->getArg(i);

        std::string arg_text;
        if (auto* default_arg = llvm::dyn_cast<clang::CXXDefaultArgExpr>(arg)) {
            llvm::raw_string_ostream stream(arg_text);
            default_arg->getExpr()->printPretty(stream, nullptr, Policy);
            stream.flush();
        } else {
            llvm::raw_string_ostream stream(arg_text);
            arg->printPretty(stream, nullptr, Policy);
            stream.flush();
        }

        if (param_decl->getType()->isLValueReferenceType()) {
            caller_params[i] = arg_text;
        } else {
            auto argLoc = arg->getExprLoc();
            caller_params[i] = prefix + std::to_string(src_mgr.getSpellingLineNumber(argLoc)) + "_" 
                                  + std::to_string(src_mgr.getSpellingColumnNumber(argLoc)) + "_" 
                                  + param_decl->getNameAsString();
            param_decl->getType().print(OS, Policy, caller_params[i]);
            OS << " = " << arg_text << ";\n";
            added_lines++;
        }
    }

    std::string raw_body;
    llvm::raw_string_ostream stream(raw_body);

    InlinePrinterHelper helper(ctx, prefix, target_var, caller_params, Policy);
    callee->getBody()->printPretty(stream, &helper, Policy);
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

    m_matched = true;

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
        clang::CharSourceRange::getTokenRange(stmt_to_replace->getSourceRange()), src_mgr, lang_opts);
    size_t net_lines = added_lines - std::count(orig_text.begin(), orig_text.end(), '\n');

    // Adjust line numbers for subsequent call sites in the same file
    // that shifted due to inserted code.
    for (size_t i = m_current_index + 1; i < m_call_sites.size(); i++) {
        if (m_call_sites[i].second == m_call_sites[m_current_index].second &&
            m_call_sites[i].first > m_call_sites[m_current_index].first)
            m_call_sites[i].first += net_lines;
    }

    OS.flush();
    m_rewriter.ReplaceText(stmt_to_replace->getSourceRange(), inlined_code);
    return true;
}

bool InlineVisitor::matched() const { return m_matched; }

// ─── ConstantFoldVisitor ──────────────────────────────────────────────

constantFoldVisitor::constantFoldVisitor(clang::ASTContext& context, clang::Rewriter& rewriter, bool& did_fold) : m_ctx(context), m_rewriter(rewriter), m_did_fold(did_fold) {
    rewriter.setSourceMgr(m_ctx.getSourceManager(), m_ctx.getLangOpts());
    m_did_fold = false;
}

bool constantFoldVisitor::VisitExpr(clang::Expr* expr) {
    if(m_did_fold) return true;

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
        llvm::isa<clang::CompoundLiteralExpr>(inner))
        return true;

    clang::Expr::EvalResult result;
    if (!expr->EvaluateAsRValue(result, m_ctx))
        return true;

    clang::APValue value = result.Val;

    std::string replacement{};
    llvm::raw_string_ostream OS(replacement);
    value.printPretty(OS, m_ctx, expr->getType());
    OS.flush();

    m_rewriter.ReplaceText(
        clang::CharSourceRange::getTokenRange(expr->getSourceRange()),
        replacement);

    m_did_fold = true;
    return true;
}

// ─── preprocess ────────────────────────────────────────────────────

std::string preprocess(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::string result = source;

    // Split comma-separated declarations.
    {
        clang::Rewriter rewriter;
        result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            CommaInitVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
    }

    // Hoist nested function calls (repeat until stable).
    bool did_cleanup = true;
    while (did_cleanup) {
        did_cleanup = false;
        clang::Rewriter rewriter;
        result = runInMemory(result, filename, args, [&rewriter, &did_cleanup](clang::ASTContext& ctx) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            FuncCallCleanupVisitor visitor(ctx, rewriter, did_cleanup);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
    }

    // Insert braces around single-statement control flow bodies.
    {
        static const auto style = []() {
            auto s = clang::format::getLLVMStyle();
            s.InsertBraces = true;
            return s;
        }();
        auto replacements = clang::format::reformat(
            style, result,
            {clang::tooling::Range(0, result.size())});
        auto formatted =
            clang::tooling::applyAllReplacements(result, replacements);
        if (formatted)
            result = *formatted;
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
        result = runInMemory(result, filename, args, [&rewriter, &did_fold](clang::ASTContext& ctx) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            constantFoldVisitor visitor(ctx, rewriter, did_fold);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
    }

    return result;
}

// ─── separate_functions ────────────────────────────────────────────

std::string separate_functions(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::string result;

    clang::Rewriter rewriter;
    runInMemory(source, filename, args, [&rewriter, &result](clang::ASTContext& ctx) {
        rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        FilterVisitor visitor(ctx, rewriter);
        visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        result = visitor.toJSON();
    }, rewriter);

    return result;
}

// ─── inline_functions ──────────────────────────────────────────────

InlineResult inline_functions(const std::string& source, const std::string& filename,
                              const std::vector<InlineEntry>& entries, std::vector<std::string>& args) {
    std::vector<std::pair<std::string, std::string>> oracle;
    std::vector<std::pair<size_t, std::string>> call_sites;

    for (const auto& entry : entries) {
        oracle.emplace_back(entry.caller, entry.callee);
        call_sites.emplace_back(entry.line, entry.file);
    }

    std::string result = source;
    std::vector<bool> succeeded(oracle.size(), false);

    for (size_t i = 0; i < oracle.size(); i++) {
        clang::Rewriter rewriter;
        result = runInMemory(
            result, filename, args,
            [&rewriter, &oracle, &call_sites, &succeeded, i](clang::ASTContext& ctx) {
                rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
                InlineVisitor visitor(ctx, oracle[i].first, oracle[i].second, call_sites, i, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                if(!visitor.matched())
                    llvm::outs() << "could not find any matching pair for this: " << oracle[i].first << " " << oracle[i].second << " " << call_sites[i].first << "\n";
                succeeded[i] = visitor.matched();
            },
            rewriter);
    }

    return {result, succeeded};
}

}  // namespace invisible
