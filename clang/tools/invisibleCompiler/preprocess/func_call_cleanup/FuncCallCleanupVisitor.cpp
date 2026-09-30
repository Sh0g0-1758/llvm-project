#include "preprocess/func_call_cleanup/FuncCallCleanupVisitor.h"
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

/// How a use by value of a hoisted temporary of class type, eg. passing it to a parameter
/// taken by value or returning it, makes its own object out of it: the unnamed result the
/// temporary replaces needed neither.
enum class ByValue { Move, ExplicitMove, Copy, ExplicitCopy, Impossible };

ByValue byValue(clang::Sema& sema, clang::QualType type) {
    auto* record = type->getAsCXXRecordDecl();
    if (!record || record->isDependentType()) return ByValue::Move;
    unsigned quals = type.getCVRQualifiers();
    if (clang::CXXConstructorDecl* move = sema.LookupMovingConstructor(record, quals); move && !move->isDeleted())
        return move->isExplicit() ? ByValue::ExplicitMove : ByValue::Move;
    if (clang::CXXConstructorDecl* copy = sema.LookupCopyingConstructor(record, quals); copy && !copy->isDeleted())
        return copy->isExplicit() ? ByValue::ExplicitCopy : ByValue::Copy;
    return ByValue::Impossible;
}

}  // namespace

// ─── FuncCallCleanupVisitor ────────────────────────────────────────

FuncCallCleanupVisitor::FuncCallCleanupVisitor(clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter,
                                               size_t& current_index)
    : m_ctx(ctx), m_sema(sema), m_rewriter(rewriter), m_is_uninstantiated_template(false), m_current_index(current_index), m_policy(m_ctx.getLangOpts()) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    // a hoisted temporary is declared in the caller, where the typedefs the callee's
    // return type was written with, eg. a member typedef, may not be visible.
    m_policy.PrintAsCanonical = true;
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
    const clang::Stmt* statement = nullptr;
    clang::SourceLocation nearest_compound_stmt = findInsertionLoc(call, &statement);

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

    ByValue by_value = ByValue::Move;
    if (lang_opts.CPlusPlus && call->isPRValue() && call->getType()->isRecordType() && isUsedByValue(m_ctx, call)) {
        by_value = byValue(m_sema, call->getType());
        // a value that can be neither moved nor copied, eg. holding a mutex, is only
        // initialized from an unnamed result, `take(make())` is left as written.
        if (by_value == ByValue::Impossible) return;
    }

    auto call_text =
        clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(call->getSourceRange()), src_mgr, lang_opts)
            .str();

    // `obj->m` calls `obj.operator->()` and applies `->` to what it returns, the
    // text of the call being `obj->`.
    std::string arrow;
    if (const auto* op_call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(call);
        op_call && op_call->getOperator() == clang::OO_Arrow) {
        call_text = "(" +
                    clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(op_call->getArg(0)->getSourceRange()),
                                                src_mgr, lang_opts)
                        .str() +
                    ").operator->()";
        arrow = "->";
    }

    std::string ret_var{};
    std::string clean_call{};
    bool direct_init = false;
    llvm::raw_string_ostream out(clean_call);

    if (!call->getType()->isVoidType()) {
        clang::QualType ret_ty = call->getType();
        ret_var = "__func_hoist_ret__" + std::to_string(m_current_index++);

        clang::QualType decl_ty = ret_ty;
        bool needs_cast = false;

        /**
         * functions can return an lvalue, an xvalue or a prvalue.
         * For an lvalue, we need to convert the type to reflect 
         * taking an lvalue reference. Same for an xvalue. For a prvalue
         * we create a copy as taking it by an rvalue reference would lead
         * to an infinite loop of hoisting. Now we need to forward this result
         * to the place where its used. Hence for an xvalue and a prvalue, we wmit
         * an extra cast which converts it to an rvalue. An lvalue is left
         */
        if (lang_opts.CPlusPlus) {
            if (call->isLValue()) {
                decl_ty = m_ctx.getLValueReferenceType(ret_ty);
            } else if (call->isXValue()) {
                decl_ty = m_ctx.getRValueReferenceType(ret_ty);
                needs_cast = true;
            }
        }

        std::string name = ret_var;
        decl_ty.print(out, m_policy, ret_var);

        // a conversion operator applied implicitly, eg. by `if (obj)`, has no source text
        // of its own and may be explicit, which copy-initialization can't call. Direct
        // initialization can, and is what a condition does. It considers constructors
        // as well though, so a conversion that isn't explicit copy-initializes.
        if (const auto* member_call = llvm::dyn_cast<clang::CXXMemberCallExpr>(call)) {
            const auto* member = llvm::dyn_cast<clang::MemberExpr>(member_call->getCallee());
            const auto* conversion = member ? llvm::dyn_cast<clang::CXXConversionDecl>(member->getMemberDecl()) : nullptr;
            direct_init = conversion && conversion->isExplicit() && member->getMemberLoc().isInvalid();
        }
        out << (direct_init ? "(" : " = ");

        if (lang_opts.CPlusPlus) {
            if (call->isPRValue()) {
                decl_ty = m_ctx.getRValueReferenceType(ret_ty);
                needs_cast = true;
            }
        }

        if (needs_cast) {
            std::string ty_str;
            llvm::raw_string_ostream ty_os(ty_str);
            decl_ty.print(ty_os, m_policy);
            ty_os.flush();
            std::string value_ty = ret_ty.getUnqualifiedType().getAsString(m_policy);
            std::string lvalue_ty = ret_ty.getAsString(m_policy) + " &";
            // an `explicit` constructor is only called by direct initialization, and a
            // temporary that can't be moved is copied, `return tmp;` would still move it
            switch (by_value) {
            case ByValue::Move: ret_var = "static_cast<" + ty_str + ">(" + name + ")"; break;
            case ByValue::ExplicitMove: ret_var = value_ty + "(static_cast<" + ty_str + ">(" + name + "))"; break;
            case ByValue::Copy: ret_var = "static_cast<" + lvalue_ty + ">(" + name + ")"; break;
            case ByValue::ExplicitCopy: ret_var = value_ty + "(static_cast<" + lvalue_ty + ">(" + name + "))"; break;
            case ByValue::Impossible: break;
            }
        }
    } else {
        ret_var = "(void)0";
    }
    out << call_text << (direct_init ? ");\n" : ";\n");
    out.flush();

    if(nearest_compound_stmt == call->getSourceRange().getBegin()) {
        // This case is called when we have functions being called on the 
        // result of the previous function
        // ie. a().b().c()
        m_rewriter.ReplaceText(call->getSourceRange(), clean_call + ret_var + arrow);
    } else {
        m_rewriter.InsertTextBefore(nearest_compound_stmt, clean_call);
        m_rewriter.ReplaceText(call->getSourceRange(), ret_var + arrow);
    }
    // the temporary of a statement directly in a switch would be in scope at the case
    // labels that follow, which can't jump past its initialization
    auto parents = m_ctx.getParents(*statement);
    const auto* compound = parents.empty() ? nullptr : parents[0].get<clang::CompoundStmt>();
    if (compound && !llvm::isa<clang::DeclStmt>(statement) && !m_ctx.getParents(*compound).empty() &&
        m_ctx.getParents(*compound)[0].get<clang::SwitchStmt>()) {
        const clang::SourceManager& src_mgr = m_ctx.getSourceManager();
        clang::SourceLocation end = clang::Lexer::findLocationAfterToken(statement->getEndLoc(), clang::tok::semi,
                                                                         src_mgr, m_ctx.getLangOpts(), false);
        if (end.isInvalid()) end = clang::Lexer::getLocForEndOfToken(statement->getEndLoc(), 0, src_mgr, m_ctx.getLangOpts());
        m_rewriter.InsertTextBefore(nearest_compound_stmt, "{\n");
        m_rewriter.InsertTextAfter(end, "\n}");
    }
    m_last_update_loc = nearest_compound_stmt;
    m_last_call_loc = call->getSourceRange();
}

clang::SourceLocation FuncCallCleanupVisitor::findInsertionLoc(const clang::CallExpr* call, const clang::Stmt** statement) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*call);
    const clang::Stmt* last_stmt = call;

    while (true) {
        auto parents = m_ctx.getParents(current);
        if (parents.empty())
            return {};
        const auto& parent = parents[0];
        // a call in a constructor initializer, a default member initializer of a local
        // class or the signature of a function declared inside a block can't be hoisted
        // into the enclosing function, whose variables aren't visible there.
        if (parent.get<clang::CXXCtorInitializer>() || parent.get<clang::TagDecl>() || parent.get<clang::FunctionDecl>())
            return {};
        if (parent.get<clang::CompoundStmt>() != nullptr) {
            *statement = last_stmt;
            return last_stmt->getBeginLoc();
        }
        if (const auto* stmt = parent.get<clang::Stmt>())
            last_stmt = stmt;

        current = parent;
    }
}

// These node types are only relevant for C++
// CXXBindTemporaryExpr -> It marks the creation of a temporary with a non-trivial
//                         destructor
// MaterializeTemporaryExpr -> Emitted when a prvalue needs to be converted to an 
//                             xvalue. Usually when its address is needed.
// ExprWithCleanups -> Enclosing an expression. Indicates there is some temporary
//                     created inside this expression that needs to be cleaned up
//                     ie. its destructor needs to be called. 
// ImplicitCastExpr -> A NoOp of adding a const to a function return type. This can
//                     be needed in cases when say a class's copy constructor needs
//                     to be called. 
// ParenExpr        -> relevant for both C/C++. Ignore enclosing paranthesis
// ExplicitCastExpr -> an explicit C++ conversion function within a class needs to retain
//                     the explicit cast. We should not hoist such conversion operators as
//                     they will lead to an infinite loop of hoisting. 
bool FuncCallCleanupVisitor::isTransparentParent(const clang::Stmt* s, bool isExplicitCoversionOperator) {
    return llvm::isa<clang::ParenExpr,
                     clang::ImplicitCastExpr,
                     clang::ExprWithCleanups,
                     clang::CXXBindTemporaryExpr,
                     clang::MaterializeTemporaryExpr>(s) || 
           (isExplicitCoversionOperator ? 
            (llvm::isa<clang::ExplicitCastExpr>(s)) : false);
}

clang::DynTypedNode FuncCallCleanupVisitor::getSemanticParent(const clang::Stmt* node, bool isExplicitCoversionOperator) {
    for (const clang::Stmt* child = node;;) {
        auto parents = m_ctx.getParents(*child);
        if (parents.empty()) return {};
        const auto* parent = parents[0].get<clang::Stmt>();
        if (!parent || !isTransparentParent(parent, isExplicitCoversionOperator)) return parents[0];
        child = parent;
    }
}

bool FuncCallCleanupVisitor::isTopLevelCall(const clang::CallExpr* call) {
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

}  // namespace invisible
