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

ConstantFoldVisitor::ConstantFoldVisitor(clang::ASTContext& context, clang::Rewriter& rewriter) : m_ctx(context), m_rewriter(rewriter) { }

// whether an expression was folded in this iteration
bool ConstantFoldVisitor::didFold() {
    return (m_last_update_loc_begin.getRawEncoding() != 0) && (m_last_update_loc_end.getRawEncoding() != 0);
}

// a parameter that inherits its default argument (eg. the redeclaration of a member in a
// deferred specialization) points to the same expression as the declaration it was written
// on. Folding it again would edit the same source text twice, so only the declaration that
// wrote the default folds it.
bool ConstantFoldVisitor::TraverseParmVarDecl(clang::ParmVarDecl* param) {
    if (!param->hasInheritedDefaultArg())
        return clang::RecursiveASTVisitor<ConstantFoldVisitor>::TraverseParmVarDecl(param);
    if (clang::TypeSourceInfo* type_info = param->getTypeSourceInfo())
        return TraverseTypeLoc(type_info->getTypeLoc());
    return true;
}

// the exception specification of a function is left as written. The one of the
// specialization of a member of a class template names the member to have clang
// work out the exception specification of the member, see DeclBeforeDef.
bool ConstantFoldVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    clang::SourceRange spec = func->getExceptionSpecSourceRange();
    if (spec.isValid()) m_exception_specs.push_back(spec);
    return true;
}

// see VisitExpr for why these parents keep their operand as written
bool ConstantFoldVisitor::skipIfBadParent(clang::Expr* expr) {
    if (!expr) return false;

    auto parents = m_ctx.getParents(*expr);
    if (parents.empty()) return false;

    const auto& parent = parents[0];
    return parent.get<clang::TypeOfExprTypeLoc>() != nullptr || 
           parent.get<clang::TypeOfExprType>()    != nullptr ||
           parent.get<clang::TypeOfType>()        != nullptr ||
           parent.get<clang::MemberExpr>()        != nullptr;
}

// a literal is its own value, eg. `5`, `"s"` or `{1, 2}`, folding it would change nothing
bool ConstantFoldVisitor::isConstantLiteral(clang::Expr* inner) {
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

bool ConstantFoldVisitor::VisitExpr(clang::Expr* expr) {
    if (expr->getSourceRange().isInvalid()) return true;
    if ((expr->getSourceRange().getBegin() >= m_last_update_loc_begin) && (expr->getSourceRange().getEnd() <= m_last_update_loc_end)) return true;
    if (llvm::any_of(m_exception_specs, [&](clang::SourceRange spec) {
            return expr->getBeginLoc() >= spec.getBegin() && expr->getEndLoc() <= spec.getEnd();
        }))
        return true;

    clang::Expr* inner = expr->IgnoreImpCasts();
    if (isConstantLiteral(inner)) return true;

    clang::QualType exprTy = expr->getType();

    // Guard against null/invalid types (e.g. uninstantiated templates)
    if (exprTy.isNull() || exprTy->isDependentType() || exprTy->isUndeducedType() || exprTy->isVoidType())
        return true;

    // Don't fold enums into their underlying type. Note that we don't care if
    // the underlying type is enum as we should use enum vals to do arith ops
    // if the final type of the expr is an arith type
    if (exprTy->isEnumeralType())
        return true;

    // only a number is folded, see below, and what else is constant needn't be evaluated
    if (!exprTy->isIntegerType() && !exprTy->isRealFloatingType())
        return true;

    // -2 -> negop (numeric literal)
    // The Pass will loop indefinitely if it sees a negation literal followed by a
    // numeric expression. This is because C grammar parses it as a unop. It will then
    // emit it as a unop only. We match on this and exit early if that is the case. 
    // We don't need to worry about parentheses and implicit casts as they will get removed
    // by themselves as the pass goes to deeper expressions.
    if (auto* unary = llvm::dyn_cast<clang::UnaryOperator>(expr->IgnoreParenImpCasts()))
        if (unary->getOpcode() == clang::UO_Minus)
            if (llvm::isa<clang::IntegerLiteral>(unary->getSubExpr()) ||
                llvm::isa<clang::FloatingLiteral>(unary->getSubExpr()))
                return true;

    // If this expr's address is being taken, folding would turn `&arr[i]` into
    // `&<constant>`, which is invalid (can't take the address of an rvalue literal)
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
    if (skipIfBadParent(expr)) {
        return true;
    }

    if (expr->isValueDependent() || inner->isValueDependent())
        return true;
    // the value of the expression, if clang can compute it and computing it has no side effects
    // or undefined behavior, which replacing it with its value would drop
    clang::Expr::EvalResult result;
    if (!expr->EvaluateAsRValue(result, m_ctx) || result.HasSideEffects || result.HasUndefinedBehavior) return true;

    clang::APValue value = result.Val;
    // Only an integer or a floating-point value is written as a literal. Other values have no
    // literal to write, so they are left as written:
    //  - an address, eg. `&arr[0]` of a const array;
    //  - a class, union or array, eg. a call `buffer_()` returning a union, which would print as
    //    `{.ptr = nullptr}`;
    //  - a vector or a complex number.
    if (!value.isInt() && !value.isFloat())
        return true;
    // an integer type wider than 64 bits, eg. `__int128`, has no literal: `(__int128)5` would be
    // printed as `5`, an int, and the arithmetic around it would be done in int instead
    if (value.isInt() && m_ctx.getTypeSize(expr->getType()) > 64) return true;

    bool promotes = value.isInt() && !expr->getType()->isBooleanType() && m_ctx.isPromotableIntegerType(expr->getType());
    std::string replacement{};
    llvm::raw_string_ostream OS(replacement);
    if (value.isFloat()) {
        // a finite literal of the type folded in full precision, eg. `-1.0E+0F` for a float.
        // Others, eg. of __float128, are printed as a double.
        const auto* builtin = expr->getType().getCanonicalType()->getAs<clang::BuiltinType>();
        if (!builtin || !value.getFloat().isFinite()) return true;
        const char* suffix = builtin->getKind() == clang::BuiltinType::Float        ? "F"
                             : builtin->getKind() == clang::BuiltinType::Double     ? ""
                             : builtin->getKind() == clang::BuiltinType::LongDouble ? "L"
                                                                                    : nullptr;
        if (!suffix) return true;
        llvm::SmallString<32> digits;
        value.getFloat().toString(digits);
        if (digits.find_first_of(".eE") == llvm::StringRef::npos) digits += ".0";
        OS << digits << suffix;
    } else if (value.isInt() && value.getInt().isSigned() && value.getInt().isMinSignedValue() &&
               value.getInt().getBitWidth() > 1 && !expr->getType()->isBooleanType() && !promotes) {
        // the literal of the minimum is the negation of a value the type doesn't hold
        llvm::APSInt above(value.getInt() + 1, false);
        OS << "(" << above << integerSuffix(m_ctx, above, expr->getType()) << " - 1)";
    } else if (value.isInt() && expr->getType()->isBooleanType() && !m_ctx.getLangOpts().CPlusPlus) {
        // `true` isn't a keyword of C before C23
        OS << (value.getInt().getBoolValue() ? "1" : "0");
    } else {
        value.printPretty(OS, m_ctx, expr->getType());
    }
    OS.flush();
    if (value.isInt() && replacement.front() != '(') replacement += integerSuffix(m_ctx, value.getInt(), expr->getType());
    // a literal of a type that promotes to int is an int, eg. `char('$')` folded to `36` would pick
    // other overloads and deduce other template arguments, so the value is cast back to its type
    if (promotes)
        replacement = "(" + expr->getType().getCanonicalType().getUnqualifiedType().getAsString(m_ctx.getPrintingPolicy()) + ")" + replacement;

    // what is already spelled as its value is left, or the pass wouldn't converge
    clang::CharSourceRange written = exprCharRange(expr->getSourceRange(), m_ctx.getSourceManager(), m_ctx.getLangOpts());
    llvm::StringRef text = clang::Lexer::getSourceText(written, m_ctx.getSourceManager(), m_ctx.getLangOpts());
    if (text == replacement || (replacement.front() == '(' && "(" + text.str() + ")" == replacement))
        return true;

    m_last_update_loc_begin = expr->getSourceRange().getBegin();
    m_last_update_loc_end = expr->getSourceRange().getEnd();
    replaceExpr(m_rewriter, expr->getSourceRange(), replacement);

    return true;
}

// ─── constant fold ────────────────────────────────────────────

// the `constant-fold` mode, outside the preprocess pipeline: the visitor repeated until it folds
// nothing more, stopping at the last result that compiles
std::string constant_fold(const std::string& source, const std::string& filename, std::vector<std::string>& args, bool* did_change, bool* is_compilable) {
    std::string result = source;

    // constant fold mathematical expressions into their simplified form
    bool didFold = true;
    while (didFold) {
        didFold = false;
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &didFold](clang::ASTContext& ctx, clang::Sema&) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            ConstantFoldVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            didFold = visitor.didFold();
        }, rewriter);
        if(!isCompilable(tmp_result, filename, args)) {
            is_compilable ? (*is_compilable = false) : false;
            break;
        }
        result = std::move(tmp_result);

        did_change ? (*did_change ? true : (*did_change = didFold)) : false;
    }

    return result;
}

}  // namespace invisible
