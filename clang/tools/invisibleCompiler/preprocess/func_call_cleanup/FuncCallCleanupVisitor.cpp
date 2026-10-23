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

// How the variable of a hoisted call is passed on where the call's class result was used by value,
// eg. as a by-value argument or a return value. The call's unnamed result needed no move or copy.
// A named variable needs one.
enum class ByValue { Move, ExplicitMove, Copy, ExplicitCopy, Impossible };

// How a variable of class @p type is passed on by value: moved if it has a usable move
// constructor, else copied. eg. a std::unique_ptr is moved, and a class holding a std::mutex can
// be neither moved nor copied.
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

// --- FuncCallCleanupVisitor ----------------------------------------

FuncCallCleanupVisitor::FuncCallCleanupVisitor(clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter,
                                               size_t& current_index)
    : m_ctx(ctx), m_sema(sema), m_rewriter(rewriter), m_is_uninstantiated_template(false), m_current_index(current_index), m_policy(codePolicy(ctx)) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
    // types are printed canonically. The variable of a hoisted call is declared in the caller, where
    // a typedef in the callee's return type, eg. a member typedef, may not be visible.
    m_policy.PrintAsCanonical = true;
    m_policy.SuppressUnwrittenScope = true;
}

// whether this iteration hoisted a call. The pass runs again until an iteration hoists none.
bool FuncCallCleanupVisitor::didHoist() {
    return m_last_update_loc.getRawEncoding() != 0;
}

bool FuncCallCleanupVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if(func->isDependentContext()) m_is_uninstantiated_template = true;
    else m_is_uninstantiated_template = false;

    return true;
}

// Hoists @p call if it is nested in another expression. The call then gets a separate statement
// that stores its result in a variable, and the inliner can put the callee's body before that
// statement, eg.
//
//     return g(f(x)) + 1;   =>   int __func_hoist_ret__1 = f(x);
//                                int __func_hoist_ret__0 = g(static_cast<int &&>(__func_hoist_ret__1));
//                                return static_cast<int &&>(__func_hoist_ret__0) + 1;
//
// An iteration hoists at most one call per statement, here `g` first and `f` in the next iteration.
// The pass runs again until no call is nested.
bool FuncCallCleanupVisitor::VisitCallExpr(clang::CallExpr* call) {
    // in a template, a call may have no type to declare its result with, and `a + b` on
    // dependent operands may not be a call at all. The calls are hoisted in each instantiation.
    if(m_is_uninstantiated_template) return true;

    if (topLevelStatement(m_ctx, call)) return true;

    // `__mono_addr(make())` is the address of the temporary `make()`, eg. passed as the object to
    // the copy of a member function, `get__mono...(__mono_addr(make()))`. The temporary dies at the
    // end of the statement, so a variable holding its address would dangle. It isn't hoisted.
    if (const clang::FunctionDecl* callee = call->getDirectCallee();
        callee && callee->getIdentifier() && callee->getName() == "__mono_addr")
        return true;

    // a call in a branch of `?:` or right of `&&` or `||` may not run, so it isn't hoisted.
    // short-circuit-to-ir turned most of these operators into ifs. Only those it left as written,
    // eg. constant ones, are still here.
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

// Writes `T __func_hoist_ret__N = call;` before the statement that holds @p call directly in a
// block. Then replaces the call with the variable, cast back to the call's value category:
//  - an lvalue, eg. `v.at(i)` returning `int&`, goes in `int &__func_hoist_ret__N` and is used as is;
//  - an xvalue goes in `T &&__func_hoist_ret__N`, used as `static_cast<T &&>(__func_hoist_ret__N)`;
//  - a prvalue goes in `T __func_hoist_ret__N`, used as `static_cast<T &&>(__func_hoist_ret__N)`.
//    Where a class result is used by value and can't be moved, it is copied instead, see ByValue.
// A void call is replaced with `(void)0`. A type that can't be named, eg. a lambda's closure type,
// is declared `auto`.
void FuncCallCleanupVisitor::hoistCall(clang::CallExpr* call) {
    const clang::Stmt* statement = nullptr;
    bool unbraced = false;
    clang::SourceLocation nearest_compound_stmt = findInsertionLoc(call, &statement, &unbraced);

    // no statement to put the variable before, eg. for a call in a member initializer, a default
    // member initializer or a default argument. The call is left as written.
    if(nearest_compound_stmt.getRawEncoding() == 0)
        return;
    // one call per statement and iteration: this iteration already hoisted a call out of this
    // statement, so this call waits for the next one
    if(nearest_compound_stmt == m_last_update_loc)
        return;

    // a void call that starts its statement, eg. `done: f();`, isn't hoisted. The rewrite would be
    // `done: f(); (void)0;`, with the same call in the same place, and the pass would repeat forever.
    if((nearest_compound_stmt == call->getSourceRange().getBegin()) && (call->getType()->isVoidType()))
        return;

    // a call inside the call hoisted last, eg. `g()` in `f(g())`, waits for the next iteration.
    // The rewriter can't edit text it already replaced.
    if((call->getSourceRange().getBegin() >= m_last_call_loc.getBegin()) && (call->getSourceRange().getEnd() <= m_last_call_loc.getEnd()))
       return;

    auto& src_mgr = m_ctx.getSourceManager();
    const auto& lang_opts = m_ctx.getLangOpts();

    ByValue by_value = ByValue::Move;
    if (lang_opts.CPlusPlus && call->isPRValue() && call->getType()->isRecordType() && isUsedByValue(m_ctx, call)) {
        by_value = byValue(m_sema, call->getType());
        // a class that can be neither moved nor copied, eg. one holding a mutex, can only be
        // passed by value straight from the call. So `take(make())` is left as written.
        if (by_value == ByValue::Impossible) return;
    }

    auto call_text =
        clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(call->getSourceRange()), src_mgr, lang_opts)
            .str();

    // with an overloaded `operator->`, `obj->m` calls `obj.operator->()` and then applies `->m` to
    // the pointer it returns. The call's source text is only `obj->`. So the call is written out,
    // `(obj).operator->()`, and the `->` is put back after the variable:
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

        // in C++ an lvalue is declared `T &`, an xvalue `T &&` and a prvalue `T`. The xvalue and
        // the prvalue are passed on with a cast, see ByValue.
        if (lang_opts.CPlusPlus) {
            if (call->isLValue()) {
                decl_ty = m_ctx.getLValueReferenceType(ret_ty);
            } else if (call->isXValue()) {
                decl_ty = m_ctx.getRValueReferenceType(ret_ty);
                needs_cast = true;
            }
        }

        // a type that can't be named, eg. `View<T>` where T is a lambda's closure type, is declared
        // `auto` and cast with `decltype(__func_hoist_ret__N)`. An explicit move or copy constructor
        // must be called as `T(...)`, which names the type, so then the call isn't hoisted.
        bool deduced = lang_opts.CPlusPlus && !isNameableAnywhere(ret_ty);
        if (deduced && (by_value == ByValue::ExplicitMove || by_value == ByValue::ExplicitCopy)) return;
        std::string name = ret_var;
        if (deduced) out << (call->isLValue() ? "auto &" : call->isXValue() ? "auto &&" : "auto ") << ret_var;
        else decl_ty.print(out, m_policy, ret_var);

        // an explicit conversion operator that is called implicitly, eg. `operator bool` in
        // `if (obj)`, has the text `obj` only. `bool x = obj;` can't call an explicit conversion, but
        // `bool x(obj);` can, so that form is used. Other calls keep `=`: for a class type,
        // `T x(obj);` would also consider the constructors of T.
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
            // Move => `static_cast<T &&>(tmp)`, Copy => `static_cast<T &>(tmp)`. Copy needs the
            // cast too: `return tmp;` would still try the move constructor first, which fails
            // when it is deleted. An explicit constructor needs direct initialization, `T(...)`.
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
        // the call starts its statement, eg. `a().b();`, so the declaration and the use start at the
        // same location. They are written as one replacement, as two edits there would be merged.
        replaceExpr(m_rewriter, call->getSourceRange(), clean_call + ret_var + arrow);
    } else {
        m_rewriter.InsertTextBefore(nearest_compound_stmt, clean_call);
        replaceExpr(m_rewriter, call->getSourceRange(), ret_var + arrow);
    }
    // A case label or a goto must not jump past the variable of a hoisted call, eg.
    //
    //     case 1: f(g()); case 2: ...   =>   case 1: int __func_hoist_ret__0 = g(); f(...);
    //                                        case 2: ...     error: jump bypasses the initialization
    //
    // So in a switch body, or in a function with a label, the statement and its new variables go in
    // braces. Not a declaration, `int n = f(g());`, as the code after it uses `n`. Nor the last
    // statement of a statement expression, as it gives the expression its value.
    //
    // A body without braces, eg. `if (c) return g(f(x));`, gets braces too, so that the new
    // variables stay inside the if, see isUnbracedBody.
    if (unbraced || (!llvm::isa<clang::DeclStmt>(statement) && !isValueOfStatementExpr(statement) &&
                     (isDirectlyInSwitch(statement) || isInFunctionWithLabels(statement)))) {
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
// `switch (c) { case 1: f(g()); case 2: ... }`. A jump to `case 2:` would skip the variable
// hoisted from it, see hoistCall.
bool FuncCallCleanupVisitor::isDirectlyInSwitch(const clang::Stmt* statement) {
    auto parents = m_ctx.getParents(*statement);
    const auto* compound = parents.empty() ? nullptr : parents[0].get<clang::CompoundStmt>();
    if (!compound) return false;
    auto outer = m_ctx.getParents(*compound);
    return !outer.empty() && outer[0].get<clang::SwitchStmt>();
}

// Whether @p statement is the last statement of a statement expression, eg. `size(get());` in
// `int n = ({ prepare(); size(get()); });`. It gives the expression its value. In braces it would
// no longer be the last statement, and the expression would be void.
bool FuncCallCleanupVisitor::isValueOfStatementExpr(const clang::Stmt* statement) {
    auto parents = m_ctx.getParents(*statement);
    const auto* compound = parents.empty() ? nullptr : parents[0].get<clang::CompoundStmt>();
    if (!compound || compound->body_empty() || compound->body_back() != statement) return false;
    auto outer = m_ctx.getParents(*compound);
    return !outer.empty() && outer[0].get<clang::StmtExpr>();
}

// Whether the function or lambda holding @p statement has a label, eg. `done:` in
// `goto done; f(g()); done: ...`. There a goto may jump over the variable hoisted from any
// statement, see hoistCall. The answer is cached per body.
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

// Whether @p stmt holds a label. The labels of a lambda in it don't count, as a goto can't jump
// into or out of a lambda's body.
bool FuncCallCleanupVisitor::containsLabel(const clang::Stmt* stmt) {
    return stmt && (llvm::isa<clang::LabelStmt>(stmt) || llvm::any_of(stmt->children(), [this](const clang::Stmt* child) {
                        return !llvm::isa_and_nonnull<clang::LambdaExpr>(child) && containsLabel(child);
                    }));
}

// Where the variable for @p call is declared: before the statement that holds the call directly in
// a block or as an unbraced body, eg. before `return g(f());`. It goes after the statement's labels,
// eg. after `loop:` in `loop: x = g(f());`, so that a goto to `loop` runs the hoisted calls too.
clang::SourceLocation FuncCallCleanupVisitor::findInsertionLoc(const clang::CallExpr* call, const clang::Stmt** statement,
                                                               bool* unbraced) {
    clang::DynTypedNode current = clang::DynTypedNode::create(*call);
    const clang::Stmt* last_stmt = call;

    while (true) {
        auto parents = m_ctx.getParents(current);
        if (parents.empty())
            return {};
        const auto& parent = parents[0];
        // stop at a constructor initializer, a class or a function: eg. a call in a default member
        // initializer of a local class, or in a default argument of a function declared in a block.
        // Further up is the block of the enclosing function, and a variable there isn't visible here.
        if (parent.get<clang::CXXCtorInitializer>() || parent.get<clang::TagDecl>() || parent.get<clang::FunctionDecl>())
            return {};
        const auto* parent_stmt = parent.get<clang::Stmt>();
        *unbraced = parent_stmt && isUnbracedBody(parent_stmt, last_stmt);
        if (parent.get<clang::CompoundStmt>() != nullptr || *unbraced) {
            // after the labels of the statement, so that a goto to them runs the hoisted calls
            while (const auto* label = llvm::dyn_cast<clang::LabelStmt>(last_stmt)) last_stmt = label->getSubStmt();
            *statement = last_stmt;
            return last_stmt->getBeginLoc();
        }
        if (const auto* stmt = parent.get<clang::Stmt>())
            last_stmt = stmt;

        current = parent;
    }
}

}  // namespace invisible
