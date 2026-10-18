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

// eg. a std::unique_ptr result is moved, a class with a copy constructor only is copied, and a
// class holding a std::mutex can do neither
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
    : m_ctx(ctx), m_sema(sema), m_rewriter(rewriter), m_is_uninstantiated_template(false), m_current_index(current_index), m_policy(codePolicy(ctx)) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    // a hoisted temporary is declared in the caller, where the typedefs the callee's
    // return type was written with, eg. a member typedef, may not be visible.
    m_policy.PrintAsCanonical = true;
    m_policy.SuppressUnwrittenScope = true;
}

// whether a call was hoisted in this iteration, which the pass repeats until none is
bool FuncCallCleanupVisitor::didHoist() {
    return m_last_update_loc.getRawEncoding() != 0;
}

bool FuncCallCleanupVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if(func->isDependentContext()) m_is_uninstantiated_template = true;
    else m_is_uninstantiated_template = false;

    return true;
}

// Hoists a call nested in another expression, so that every call is a statement of its own whose
// result lands in a named variable, which the inliner can paste a body in front of, eg.
//
//     return g(f(x)) + 1;   =>   int __func_hoist_ret__0 = f(x);
//                                int __func_hoist_ret__1 = g(static_cast<int &&>(__func_hoist_ret__0));
//                                return __func_hoist_ret__1 + 1;
//
// The pass repeats, hoisting one call per statement and iteration, until none is left nested.
bool FuncCallCleanupVisitor::VisitCallExpr(clang::CallExpr* call) {
    // in a template, an operator on dependent operands, eg. `a + b` with an `operator+` in
    // sight, is a CXXOperatorCallExpr that may not be a call at all, and a call has no type to
    // declare its result with. The calls are hoisted in each instantiation instead. 
    if(m_is_uninstantiated_template) return true;

    if (isTopLevelCall(call)) return true;

    // `__mono_addr(make())` is the address of a temporary object, passed to a copy of a member
    // function, eg. `get__mono...(__mono_addr(make()))`. The temporary dies at the end of the
    // statement, so the address is only valid inside it and isn't hoisted into a variable
    if (const clang::FunctionDecl* callee = call->getDirectCallee();
        callee && callee->getIdentifier() && callee->getName() == "__mono_addr")
        return true;

    // a call that may not be evaluated, in a branch of `?:` or right of `&&` or `||`, which
    // short-circuit-to-ir leaves as written when it is constant, isn't hoisted out of it
    for (const clang::Stmt* child = call;;) {
        auto parents = m_ctx.getParents(*child);
        const auto* parent = parents.empty() ? nullptr : parents[0].get<clang::Expr>();
        if (!parent) break;
        if (const auto* cond = llvm::dyn_cast<clang::AbstractConditionalOperator>(parent); cond && cond->getCond() != child)
            return true;
        if (const auto* op = llvm::dyn_cast<clang::BinaryOperator>(parent); op && op->isLogicalOp() && op->getRHS() == child)
            return true;
        child = parent;
    }

    hoistCall(call);

    return true;
}

// Writes `T __func_hoist_ret__N = call;` before the statement of the nearest block that holds the
// call, and replaces the call with the variable, cast to what the call was:
//  - an lvalue, `int& at(int)`, is kept by reference, `int &__func_hoist_ret__N = v.at(i);`;
//  - an xvalue by rvalue reference, used as `static_cast<T &&>(__func_hoist_ret__N)`;
//  - a prvalue in a variable, used as `static_cast<T &&>(__func_hoist_ret__N)`, which moves it
//    where the unnamed result was used by value, or copies it if it can't be moved, see ByValue.
// A type that can't be named, eg. a lambda's closure type, is declared `auto`.
void FuncCallCleanupVisitor::hoistCall(clang::CallExpr* call) {
    const clang::Stmt* statement = nullptr;
    clang::SourceLocation nearest_compound_stmt = findInsertionLoc(call, &statement);

    // no statement to put the temporary before, eg. a call in a member initializer, a default
    // member initializer or a default argument: the call is left
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

    // `obj->m` with an overloaded `operator->` is a call whose text is only `obj->`, the
    // member access being applied to the pointer it returns. The call is written out,
    // `(obj).operator->()`, and the `->` it covers is put back after the temporary:
    //
    //     return obj->m;   =>   P *__func_hoist_ret__0 = (obj).operator->();
    //                           return __func_hoist_ret__0->m;
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
         * to the place where its used. Hence for an xvalue and a prvalue, we emit
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

        // a type that can't be named, eg. of a template specialized for a lambda's closure type,
        // is deduced, and named after the temporary where it is cast
        bool deduced = lang_opts.CPlusPlus && !isNameableAnywhere(ret_ty);
        if (deduced && (by_value == ByValue::ExplicitMove || by_value == ByValue::ExplicitCopy)) return;
        std::string name = ret_var;
        if (deduced) out << (call->isLValue() ? "auto &" : call->isXValue() ? "auto &&" : "auto ") << ret_var;
        else decl_ty.print(out, m_policy, ret_var);

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
            std::string ty_str = deduced ? "decltype(" + name + ") &&" : decl_ty.getAsString(m_policy);
            std::string value_ty = ret_ty.getUnqualifiedType().getAsString(m_policy);
            std::string lvalue_ty = deduced ? "decltype(" + name + ") &" : ret_ty.getAsString(m_policy) + " &";
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
        replaceExpr(m_rewriter, call->getSourceRange(), clean_call + ret_var + arrow);
    } else {
        m_rewriter.InsertTextBefore(nearest_compound_stmt, clean_call);
        replaceExpr(m_rewriter, call->getSourceRange(), ret_var + arrow);
    }
    // A hoisted temporary stays in scope until the end of its block, and C++ doesn't allow a jump
    // past its initialization, eg. to the next case label:
    //
    //     case 1: f(g()); case 2: ...   =>   case 1: int __func_hoist_ret__0 = g(); f(...);
    //                                        case 2: ...     error: jump bypasses the initialization
    //
    // So in a switch body, or in a function with any label, the statement and its temporaries are
    // put in a block of their own. Not a declaration, `int n = f(g());`, whose `n` the code after it
    // uses (and which no jump can cross, as it is initialized too), nor the last statement of a
    // statement expression, whose value it is.
    if (!llvm::isa<clang::DeclStmt>(statement) && !isValueOfStatementExpr(statement) &&
        (isDirectlyInSwitch(statement) || isInFunctionWithLabels(statement))) {
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

// Whether @p statement is directly in the body of a switch, eg. `f(g());` in
// `switch (c) { case 1: f(g()); case 2: ... }`, where a temporary hoisted from it would be
// jumped over by `case 2:`, see hoistCall.
bool FuncCallCleanupVisitor::isDirectlyInSwitch(const clang::Stmt* statement) {
    auto parents = m_ctx.getParents(*statement);
    const auto* compound = parents.empty() ? nullptr : parents[0].get<clang::CompoundStmt>();
    if (!compound) return false;
    auto outer = m_ctx.getParents(*compound);
    return !outer.empty() && outer[0].get<clang::SwitchStmt>();
}

// Whether @p statement is the last statement of a statement expression, eg. `size(get());` in
// `int n = ({ prepare(); size(get()); });`, which is the value of the expression: put in a block
// of its own, the value would be void.
bool FuncCallCleanupVisitor::isValueOfStatementExpr(const clang::Stmt* statement) {
    auto parents = m_ctx.getParents(*statement);
    const auto* compound = parents.empty() ? nullptr : parents[0].get<clang::CompoundStmt>();
    if (!compound || compound->body_empty() || compound->body_back() != statement) return false;
    auto outer = m_ctx.getParents(*compound);
    return !outer.empty() && outer[0].get<clang::StmtExpr>();
}

// Whether the function or lambda holding @p statement has a label, eg. `done:` in
// `goto done; f(g()); done: ...`, where a goto may jump over a temporary hoisted from any statement,
// see hoistCall. The answer is kept per body.
bool FuncCallCleanupVisitor::isInFunctionWithLabels(const clang::Stmt* statement) {
    // the body of the function or lambda the statement is in
    const clang::Stmt* body = statement;
    for (auto parents = m_ctx.getParents(*body); !parents.empty(); parents = m_ctx.getParents(parents[0])) {
        if (parents[0].get<clang::FunctionDecl>() || parents[0].get<clang::LambdaExpr>()) break;
        if (const auto* stmt = parents[0].get<clang::Stmt>()) body = stmt;
    }
    auto [found, inserted] = m_has_labels.try_emplace(body, false);
    if (inserted) found->second = containsLabel(body);
    return found->second;
}

// Whether @p stmt holds a label. The labels of a lambda in it don't count, as its body is a
// function of its own.
bool FuncCallCleanupVisitor::containsLabel(const clang::Stmt* stmt) {
    return stmt && (llvm::isa<clang::LabelStmt>(stmt) || llvm::any_of(stmt->children(), [this](const clang::Stmt* child) {
                        return !llvm::isa_and_nonnull<clang::LambdaExpr>(child) && containsLabel(child);
                    }));
}

// where the temporary goes, eg. before the return of `return g(f());`, and after the labels of
// `loop: x = g(f());`, so that a jump to `loop` makes the calls
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
            // after the labels of the statement, so that a jump to them makes the calls
            while (const auto* label = llvm::dyn_cast<clang::LabelStmt>(last_stmt)) last_stmt = label->getSubStmt();
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
// ParenExpr        -> relevant for both C/C++. Ignore enclosing parentheses
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

// eg. the VarDecl of `std::string s = make();`, past the temporary's materialization and cleanups
clang::DynTypedNode FuncCallCleanupVisitor::getSemanticParent(const clang::Stmt* node, bool isExplicitCoversionOperator) {
    for (const clang::Stmt* child = node;;) {
        auto parents = m_ctx.getParents(*child);
        if (parents.empty()) return {};
        const auto* parent = parents[0].get<clang::Stmt>();
        if (!parent || !isTransparentParent(parent, isExplicitCoversionOperator)) return parents[0];
        child = parent;
    }
}

// Whether @p call is a statement of its own already, which needs no hoisting, directly in a block:
//  - a declaration, `int x = f();`;
//  - an assignment, `x = f();`, also `a = f();` of a class, whose `=` is a call itself;
//  - a call returning void, `f();`.
// A call returning a value, `f();`, is hoisted all the same, as it may be the value of a
// statement expression, `({ ...; f(); })`. The cast around an explicit conversion operator,
// `bool b = static_cast<bool>(obj);`, is looked through: hoisting it would write the same cast
// again, and the pass would never stop.
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
