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

// --- ConstantFoldVisitor ----------------------------------------------

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

// records the `noexcept(...)` of @p func, which VisitExpr leaves as written. decl-before-def puts
// an expression naming the member, eg. `&S<int>::f`, in the `noexcept` of a member's
// specialization, so that clang computes the member's exception specification first. Folded,
// that expression would be gone.
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

// Replaces an expression with its value when clang can compute it at compile time, eg.
// `2 * 8 + 1` => `17`. The value comes from clang's Expr::EvaluateAsRValue, which does more than
// fold literals but less than constant propagation. It can read:
//  - literals and the operators on them, eg. `2 * 8 + 1` => `17`;
//  - `const` and `constexpr` variables with a constant initializer, also an element or member of
//    one, eg. `k * 2` => `10` after `const int k = 5;`, or `ps[1].b` => `4` of a const array `ps`;
//  - calls of `constexpr` functions, eg. `sq(3)` => `9`.
// It doesn't read a variable that isn't const, even one only ever set to a constant, eg.
// `local * 2` after `int local = 4;` stays. Nor does it call a function that isn't constexpr, or
// fold what depends on a parameter, eg. `n * 0`.
// Only an integer or floating point value is written back. The checks below leave everything else
// as written, each with its reason.
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

    // an enum value is left as written. Folded, it would become a number of the underlying type,
    // eg. `Color::red` => `0`, which isn't a `Color`.
    if (exprTy->isEnumeralType())
        return true;

    // only an integer or a floating point value is folded, see below, so other types aren't evaluated
    if (!exprTy->isIntegerType() && !exprTy->isRealFloatingType())
        return true;

    // `-2` isn't a literal: it is the unary `-` applied to the literal `2`. Folding it would write
    // `-2` again, and the pass would never stop.
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

    // a builtin call, eg. `__builtin_nanf("")`, is left as written. Its value may have no literal:
    // the NaN would be printed as `nan`, which clang reads as the function `nan`.
    if (auto* call = llvm::dyn_cast<clang::CallExpr>(expr->IgnoreParenImpCasts()))
        if (auto* fn = llvm::dyn_cast_or_null<clang::FunctionDecl>(call->getCalleeDecl()))
            if (fn->getBuiltinID() != 0)
                return true;
    
    // a temporary bound to a reference, eg. the argument of `f(int &&)` in `f(2 + 3)`, is left as
    // written. The expression inside it, `2 + 3`, is folded instead.
    if (llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expr)) {
        return true;
    }

    // the operand of `__typeof__(...)` and the object of a member access are left as written:
    //  - folding the operand of `__typeof__(0)` can drop its parentheses, `__typeof__0`, which
    //    doesn't compile;
    //  - with a const array `store` of structs, the object `store[1].b` of `store[1].b.a` is a
    //    constant too. Folded, it would be a braced list, `{...}.a`, which doesn't compile.
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
        // the minimum of a signed type has no literal: `-2147483648` is `-` applied to `2147483648`,
        // which doesn't fit in an int. So it is written `(-2147483647 - 1)`.
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

// --- constant fold --------------------------------------------

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
