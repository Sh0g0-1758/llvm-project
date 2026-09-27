#include "constant_fold/ConstantFoldVisitor.h"
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

// ─── ConstantFoldVisitor ──────────────────────────────────────────────

constantFoldVisitor::constantFoldVisitor(clang::ASTContext& context, clang::Rewriter& rewriter) : m_ctx(context), m_rewriter(rewriter) { }

bool constantFoldVisitor::did_fold() {
    return (m_last_update_loc_begin.getRawEncoding() != 0) && (m_last_update_loc_end.getRawEncoding() != 0);
}

// a parameter that inherits its default argument (eg. the redeclaration of a member in a
// deferred specialization) points to the same expression as the declaration it was written
// on. Folding it again would edit the same source text twice, so only the declaration that
// wrote the default folds it.
bool constantFoldVisitor::TraverseParmVarDecl(clang::ParmVarDecl* param) {
    if (!param->hasInheritedDefaultArg())
        return clang::RecursiveASTVisitor<constantFoldVisitor>::TraverseParmVarDecl(param);
    if (clang::TypeSourceInfo* type_info = param->getTypeSourceInfo())
        return TraverseTypeLoc(type_info->getTypeLoc());
    return true;
}

bool constantFoldVisitor::skip_if_bad_parent(clang::Expr* expr) {
    if (!expr) return false;

    auto parents = m_ctx.getParents(*expr);
    if (parents.empty()) return false;

    const auto& parent = parents[0];
    return parent.get<clang::TypeOfExprTypeLoc>() != nullptr || 
           parent.get<clang::TypeOfExprType>()    != nullptr ||
           parent.get<clang::TypeOfType>()        != nullptr ||
           parent.get<clang::MemberExpr>()        != nullptr;
}

bool constantFoldVisitor::is_constant_literal(clang::Expr* inner) {
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
    if (exprTy.isNull() || exprTy->isDependentType() || exprTy->isUndeducedType() || exprTy->isVoidType())
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
    
    // Don't fold expressions whose job is to convert an rvalue to an xvalue.
    // for instance, an rvalue arg to a function that takes an rvalue reference.
    if (llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expr)) {
        return true;
    }

    // Don't fold expressions that have typeof as their immediate parent. 
    // This is because it can convert a valid expression __typeof__(0) into
    // something invalid, eg. __typeof__0 ie. stripping the parens. 
    // Don't fold expressions that have MemberExpr as their immediate parent. 
    // This is because it can convert an expression that should result in an 
    // lvalue into an initializer list. for instance consider : 
    // struct a ...
    // struct b ... {uses struct a}
    // const struct b store[2] = {{{.....}}}
    // ... function foo begins
    // Type result = store[1].<b_member>.<a_member>
    // ...
    // This can be converted to : 
    // Type result = {...}.<a_member>
    // without this check. This is because the lvalue to rvalue cast is 
    // after the access to the a member. Thus before it, it does not have
    // access to the information as to whether or not its an lvalue or not
    // and if it can replace it, it does. 
    if (skip_if_bad_parent(expr)) {
        return true;
    }

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
    if (value.isInt()) replacement += integerSuffix(m_ctx, value.getInt(), expr->getType());

    // ── DEBUG ────────────────────────────────────────────────────────────────
    // llvm::errs() << "\n=== [constantFold] AST of expression being folded ===\n";
    // expr->dump();                                          // full AST subtree
    // ─────────────────────────────────────────────────────────────────────────

    m_last_update_loc_begin = expr->getSourceRange().getBegin();
    m_last_update_loc_end = expr->getSourceRange().getEnd();
    m_rewriter.ReplaceText(expr->getSourceRange(), replacement);

    return true;
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

}  // namespace invisible
