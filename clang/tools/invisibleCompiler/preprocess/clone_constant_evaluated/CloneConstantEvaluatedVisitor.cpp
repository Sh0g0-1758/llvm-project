#include "preprocess/clone_constant_evaluated/CloneConstantEvaluatedVisitor.h"
#include "helpers.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <algorithm>

namespace invisible {

namespace {

constexpr const char* kPrefix = "__invisible_clone_";
constexpr const char* kTag = "__invisible_clone_tag";
constexpr const char* kConstruct = "__invisible_clone_construct";
constexpr const char* kKept = "__attribute__((annotate(\"invisible_constant_evaluated\"))) ";

// the order of the edits at one offset: the insertions after an expression, inner
// ones first, then the insertions before one, outer ones first, then the replacements.
constexpr long kBefore = 1L << 40;
constexpr long kReplace = 1L << 41;

const char* operatorName(clang::OverloadedOperatorKind kind) {
    switch (kind) {
#define OVERLOADED_OPERATOR(Name, Spelling, Token, Unary, Binary, MemberOnly) \
    case clang::OO_##Name:                                                    \
        return #Name;
#include "clang/Basic/OperatorKinds.def"
    default:
        return "";
    }
}

}  // namespace

// ─── CloneConstantEvaluatedVisitor ────────────────────────────────

CloneConstantEvaluatedVisitor::CloneConstantEvaluatedVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) {
    const auto& src_mgr = ctx.getSourceManager();
    m_text = src_mgr.getBufferData(src_mgr.getMainFileID());
}

bool CloneConstantEvaluatedVisitor::offsetOf(clang::SourceLocation loc, unsigned& offset) const {
    const auto& src_mgr = m_ctx.getSourceManager();
    loc = src_mgr.getExpansionLoc(loc);
    if (loc.isInvalid() || !src_mgr.isInMainFile(loc)) return false;
    offset = src_mgr.getFileOffset(loc);
    return true;
}

bool CloneConstantEvaluatedVisitor::rangeOf(clang::SourceRange range, Range& out) const {
    const auto& src_mgr = m_ctx.getSourceManager();
    clang::SourceLocation end =
        clang::Lexer::getLocForEndOfToken(src_mgr.getExpansionLoc(range.getEnd()), 0, src_mgr, m_ctx.getLangOpts());
    return offsetOf(range.getBegin(), out.first) && offsetOf(end, out.second) && out.first < out.second;
}

bool CloneConstantEvaluatedVisitor::declRange(const clang::FunctionDecl* redecl, Range& out) const {
    // deduction declares a specialization at its template before it is explicitly specialized
    const clang::FunctionTemplateDecl* primary = redecl->getPrimaryTemplate();
    if (primary && redecl->getBeginLoc() == primary->getTemplatedDecl()->getBeginLoc()) return false;
    const clang::Decl* decl = redecl;
    if (const auto* tmpl = redecl->getDescribedFunctionTemplate()) decl = tmpl;
    if (!rangeOf(decl->getSourceRange(), out)) return false;
    if (redecl->doesThisDeclarationHaveABody()) return true;
    size_t semi = m_text.find_first_not_of(" \t\r\n", out.second);
    if (semi != llvm::StringRef::npos && m_text[semi] == ';') out.second = semi + 1;
    return true;
}

void CloneConstantEvaluatedVisitor::addRange(std::vector<Range>& ranges, const clang::Stmt* stmt) {
    Range range;
    if (stmt && rangeOf(stmt->getSourceRange(), range)) ranges.push_back(range);
}

void CloneConstantEvaluatedVisitor::addUse(Kind kind, clang::SourceLocation loc, const clang::FunctionDecl* function,
                                           const clang::Expr* expr, clang::SourceRange range) {
    Use use{0, kind, function, expr, {0, 0}, 0, m_instantiating > 0};
    if ((!function && kind != Kind::Body && kind != Kind::Dependent) || !offsetOf(loc, use.offset)) return;
    if (range.isValid() && !rangeOf(range, use.range)) return;
    m_uses.push_back(use);
}

void CloneConstantEvaluatedVisitor::block(Kind kind, clang::SourceLocation loc) {
    unsigned offset;
    if (offsetOf(loc, offset)) m_blocked.insert({offset, kind});
}

// ─── collection ───────────────────────────────────────────────────

bool CloneConstantEvaluatedVisitor::TraverseDecl(clang::Decl* decl) {
    bool instantiated = false;
    if (const auto* function = llvm::dyn_cast_or_null<clang::FunctionDecl>(decl))
        instantiated = function->isTemplateInstantiation();
    else if (const auto* record = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(decl))
        instantiated = clang::isTemplateInstantiation(record->getTemplateSpecializationKind());
    else if (const auto* var = llvm::dyn_cast_or_null<clang::VarDecl>(decl))
        instantiated = clang::isTemplateInstantiation(var->getTemplateSpecializationKind());
    m_instantiating += instantiated;
    bool result = clang::RecursiveASTVisitor<CloneConstantEvaluatedVisitor>::TraverseDecl(decl);
    m_instantiating -= instantiated;
    return result;
}

bool CloneConstantEvaluatedVisitor::TraverseTemplateArgumentLoc(const clang::TemplateArgumentLoc& loc) {
    if (loc.getArgument().getKind() == clang::TemplateArgument::Expression)
        addRange(m_constant, loc.getSourceExpression());
    return clang::RecursiveASTVisitor<CloneConstantEvaluatedVisitor>::TraverseTemplateArgumentLoc(loc);
}

bool CloneConstantEvaluatedVisitor::VisitDecl(clang::Decl* decl) {
    // what a dependent name can be found as: parameters, locals and template
    // parameters can't be found by a lookup that is deferred to an instantiation.
    if (const auto* named = llvm::dyn_cast<clang::NamedDecl>(decl); named && named->getIdentifier()) {
        const auto* var = llvm::dyn_cast<clang::VarDecl>(named);
        if (const auto* function = llvm::dyn_cast<clang::FunctionDecl>(named)) {
            if (!isInstantiatedCopy(function)) m_named[named->getName().str()].functions.insert(patternOf(function));
        }
        else if (!llvm::isa<clang::FunctionTemplateDecl, clang::UsingDecl, clang::UsingShadowDecl,
                             clang::TemplateTypeParmDecl, clang::NonTypeTemplateParmDecl,
                             clang::TemplateTemplateParmDecl>(named) &&
                 !(var && var->isLocalVarDeclOrParm()))
            m_named[named->getName().str()].others = true;
    }
    // constant contexts
    if (const auto* assertion = llvm::dyn_cast<clang::StaticAssertDecl>(decl))
        addRange(m_constant, assertion->getAssertExpr());
    if (const auto* enumerator = llvm::dyn_cast<clang::EnumConstantDecl>(decl))
        addRange(m_constant, enumerator->getInitExpr());
    if (const auto* field = llvm::dyn_cast<clang::FieldDecl>(decl); field && field->isBitField())
        addRange(m_constant, field->getBitWidth());
    if (auto* var = llvm::dyn_cast<clang::VarDecl>(decl)) visitVar(var);
    if (auto* function = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
        addRange(m_constant, function->getTrailingRequiresClause().ConstraintExpr);
        // RecursiveASTVisitor doesn't traverse explicit specifiers
        if (clang::Expr* explicit_expr = clang::ExplicitSpecifier::getFromDecl(function).getExpr()) {
            addRange(m_constant, explicit_expr);
            TraverseStmt(explicit_expr);
        }
        // a use during template argument deduction, eg. in an enable_if, leaves no
        // expression behind, only the point where the specialization was first needed.
        if (function->isTemplateInstantiation()) addUse(Kind::Point, function->getPointOfInstantiation(), function);
    }
    if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(decl); method && method->size_overridden_methods())
        m_overriders.push_back(method);
    // `: m(1)` constructs a member of class type for some instantiations only
    if (const auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(decl))
        for (const clang::CXXCtorInitializer* init : ctor->inits()) {
            if (init->isWritten() && init->getInit() &&
                !llvm::isa<clang::CXXConstructExpr>(init->getInit()->IgnoreImplicit()))
                block(Kind::Construct, init->getLParenLoc());
            if (const clang::FieldDecl* member = init->getMember(); member && init->isWritten())
                addDependentInit(Kind::Construct, init->getLParenLoc(), member->getTypeSourceInfo(), init->getInit(),
                                 init->getRParenLoc(), !llvm::isa<clang::ParenListExpr>(init->getInit()) ||
                                                           llvm::cast<clang::ParenListExpr>(init->getInit())->getNumExprs());
        }
    if (const auto* tmpl = llvm::dyn_cast<clang::TemplateDecl>(decl)) {
        if (const auto* params = tmpl->getTemplateParameters()) addRange(m_constant, params->getRequiresClause());
        if (const auto* concept_decl = llvm::dyn_cast<clang::ConceptDecl>(tmpl))
            addRange(m_constant, concept_decl->getConstraintExpr());
    }
    for (const clang::AlignedAttr* aligned : decl->specific_attrs<clang::AlignedAttr>())
        if (aligned->isAlignmentExpr()) addRange(m_constant, aligned->getAlignmentExpr());
    return true;
}

void CloneConstantEvaluatedVisitor::addDependentInit(Kind kind, clang::SourceLocation at,
                                                     const clang::TypeSourceInfo* type, const clang::Expr* init,
                                                     clang::SourceLocation end, bool args) {
    Range written, arg_range;
    if (m_instantiating || !type || !init || !type->getType()->isDependentType() ||
        type->getType()->isReferenceType() || type->getType()->getContainedAutoType() ||
        llvm::isa<clang::InitListExpr>(init) || !rangeOf(type->getTypeLoc().getSourceRange(), written))
        return;
    unsigned offset, last;
    if (kind == Kind::Construct ? !offsetOf(end, last) : !rangeOf(init->getSourceRange(), arg_range)) return;
    if (!offsetOf(kind == Kind::Construct ? at : init->getBeginLoc(), offset)) return;
    m_dependent_inits[{offset, kind}] = {m_text.substr(written.first, written.second - written.first).str(),
                                         kind == Kind::Construct ? last : arg_range.second, args};
}

void CloneConstantEvaluatedVisitor::visitVar(clang::VarDecl* var) {
    if (var->isConstexpr() || var->hasAttr<clang::ConstInitAttr>() || var->isUsableInConstantExpressions(m_ctx))
        addRange(m_constant, var->getInit());
    if (const auto* record = var->getType()->getAsCXXRecordDecl())
        if (record->hasDefinition() && !var->getType()->isDependentType())
            addUse(Kind::Unspelled, var->getLocation(), record->getDestructor());
    if (var->getInitStyle() == clang::VarDecl::CInit)
        addDependentInit(Kind::Converting, {}, var->getTypeSourceInfo(), var->getInit(), {}, true);
    else if (const auto* list = llvm::dyn_cast_or_null<clang::ParenListExpr>(var->getInit()))
        addDependentInit(Kind::Construct, list->getLParenLoc(), var->getTypeSourceInfo(), list, list->getRParenLoc(),
                         list->getNumExprs());
    // `T x(1)` constructs an object of class type for some T only
    const clang::Expr* init = var->getInit();
    unsigned name_end;
    if (init && var->getInitStyle() != clang::VarDecl::CInit && !init->isTypeDependent() &&
        !llvm::isa<clang::CXXConstructExpr>(init->IgnoreImplicit()) &&
        offsetOf(clang::Lexer::getLocForEndOfToken(var->getLocation(), 0, m_ctx.getSourceManager(), m_ctx.getLangOpts()),
                 name_end)) {
        size_t paren = m_text.find_first_not_of(" \t\r\n", name_end);
        if (paren != llvm::StringRef::npos) m_blocked.insert({unsigned(paren), Kind::Construct});
    }
}

bool CloneConstantEvaluatedVisitor::VisitTypeLoc(clang::TypeLoc loc) {
    if (auto array = loc.getAs<clang::ConstantArrayTypeLoc>()) addRange(m_constant, array.getSizeExpr());
    if (auto array = loc.getAs<clang::DependentSizedArrayTypeLoc>()) addRange(m_constant, array.getSizeExpr());
    if (auto proto = loc.getAs<clang::FunctionProtoTypeLoc>()) addRange(m_constant, proto.getTypePtr()->getNoexceptExpr());
    if (auto decltype_loc = loc.getAs<clang::DecltypeTypeLoc>()) addRange(m_unevaluated, decltype_loc.getUnderlyingExpr());
    return true;
}

bool CloneConstantEvaluatedVisitor::VisitStmt(clang::Stmt* stmt) {
    // constant contexts and unevaluated operands
    if (const auto* if_stmt = llvm::dyn_cast<clang::IfStmt>(stmt); if_stmt && if_stmt->isConstexpr())
        addRange(m_constant, if_stmt->getCond());
    if (const auto* case_stmt = llvm::dyn_cast<clang::CaseStmt>(stmt)) {
        addRange(m_constant, case_stmt->getLHS());
        addRange(m_constant, case_stmt->getRHS());
    }
    if (const auto* constant = llvm::dyn_cast<clang::ConstantExpr>(stmt); constant && constant->isImmediateInvocation())
        addRange(m_constant, constant);
    if (const auto* noexcept_expr = llvm::dyn_cast<clang::CXXNoexceptExpr>(stmt))
        addRange(m_unevaluated, noexcept_expr->getOperand());
    if (const auto* trait = llvm::dyn_cast<clang::UnaryExprOrTypeTraitExpr>(stmt); trait && !trait->isArgumentType())
        addRange(m_unevaluated, trait->getArgumentExpr());
    if (const auto* type_id = llvm::dyn_cast<clang::CXXTypeidExpr>(stmt);
        type_id && !type_id->isTypeOperand() && !type_id->isPotentiallyEvaluated())
        addRange(m_unevaluated, type_id->getExprOperand());
    if (llvm::isa<clang::RequiresExpr>(stmt)) addRange(m_unevaluated, stmt);
    if (auto* expr = llvm::dyn_cast<clang::Expr>(stmt)) {
        visitUse(expr);
        visitBlocker(expr);
    }
    return true;
}

void CloneConstantEvaluatedVisitor::visitUse(clang::Expr* expr) {
    if (auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(expr); ref && !m_operator_callees.count(ref)) {
        if (auto* function = llvm::dyn_cast<clang::FunctionDecl>(ref->getDecl()))
            addUse(Kind::Name, ref->getLocation(), function, ref, ref->getNameInfo().getSourceRange());
        else
            block(Kind::Name, ref->getLocation());
    } else if (auto* member = llvm::dyn_cast<clang::MemberExpr>(expr)) {
        auto* function = llvm::dyn_cast<clang::FunctionDecl>(member->getMemberDecl());
        if (member->isArrow() && member->getBase()->getType()->isPointerType())
            block(Kind::Operator, member->getOperatorLoc());
        if (!function)
            block(Kind::Name, member->getMemberLoc());
        else if (member->getMemberLoc().isValid())
            addUse(Kind::Name, member->getMemberLoc(), function, member, member->getMemberNameInfo().getSourceRange());
        else
            // an implicit conversion is called on its object
            addUse(Kind::Conversion, member->getBase()->getBeginLoc(), function, member->getBase());
    } else if (auto* call = llvm::dyn_cast<clang::CXXOperatorCallExpr>(expr)) {
        m_operator_callees.insert(call->getCallee()->IgnoreImpCasts());
        addUse(Kind::Operator, call->getOperatorLoc(), call->getDirectCallee(), call);
    } else if (auto* rewritten = llvm::dyn_cast<clang::CXXRewrittenBinaryOperator>(expr)) {
        // `a != b` calls `!(a == b)`, `a < b` calls `(a <=> b) < 0`
        const clang::Expr* semantic = rewritten->getSemanticForm()->IgnoreImplicit();
        m_operator_callees.insert(semantic);
        std::string before, after;
        if (const auto* negation = llvm::dyn_cast<clang::UnaryOperator>(semantic)) {
            before = "!";
            semantic = negation->getSubExpr()->IgnoreImplicit();
        } else if (const auto* comparison = llvm::dyn_cast<clang::BinaryOperator>(semantic)) {
            before = "(";
            after = " " + comparison->getOpcodeStr().str() + " 0)";
            semantic = comparison->getLHS()->IgnoreImplicit();
        }
        if (const auto* inner = llvm::dyn_cast<clang::CXXOperatorCallExpr>(semantic))
            m_rewritten[inner] = {before, after};
        // the operands of a reversed one are called in the opposite order
        if (rewritten->isReversed()) block(Kind::Operator, rewritten->getOperatorLoc());
    } else if (auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(expr)) {
        clang::SourceLocation paren = construct->getParenOrBraceRange().getBegin();
        bool converting = construct->getNumArgs() && !llvm::isa<clang::CXXDefaultArgExpr>(construct->getArg(0)) &&
                          !construct->isListInitialization() &&
                          std::all_of(construct->arg_begin() + 1, construct->arg_end(),
                                      [](const clang::Expr* arg) { return llvm::isa<clang::CXXDefaultArgExpr>(arg); });
        if (paren.isValid())
            addUse(Kind::Construct, paren, construct->getConstructor(), construct);
        else if (converting)
            addUse(Kind::Converting, construct->getArg(0)->getBeginLoc(), construct->getConstructor(), construct);
        else
            addUse(Kind::Unspelled, construct->getLocation(), construct->getConstructor());
    } else if (auto* temporary = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(expr)) {
        addUse(Kind::Unspelled, temporary->getBeginLoc(), temporary->getTemporary()->getDestructor());
    } else if (auto* init = llvm::dyn_cast<clang::CXXDefaultInitExpr>(expr)) {
        if (const clang::Expr* initializer = init->getField()->getInClassInitializer())
            addUse(Kind::Body, init->getUsedLocation(), nullptr, nullptr, initializer->getSourceRange());
    } else if (auto* overload = llvm::dyn_cast<clang::OverloadExpr>(expr)) {
        if (overload->getName().isIdentifier())
            addUse(Kind::Dependent, overload->getNameLoc(), nullptr, overload, overload->getNameInfo().getSourceRange());
    } else if (auto* scoped = llvm::dyn_cast<clang::DependentScopeDeclRefExpr>(expr)) {
        if (scoped->getDeclName().isIdentifier())
            addUse(Kind::Dependent, scoped->getLocation(), nullptr, scoped, scoped->getNameInfo().getSourceRange());
    } else if (auto* dependent = llvm::dyn_cast<clang::CXXDependentScopeMemberExpr>(expr)) {
        if (dependent->getMember().isIdentifier())
            addUse(Kind::Dependent, dependent->getMemberLoc(), nullptr, dependent,
                   dependent->getMemberNameInfo().getSourceRange());
    } else if (auto* object = llvm::dyn_cast<clang::CXXUnresolvedConstructExpr>(expr)) {
        // `is_same<T, int>{}` in `enable_if<is_same<T, int>{}>` is converted in every instantiation
        unsigned offset;
        if (object->getTypeAsWritten()->getAs<clang::TemplateSpecializationType>() &&
            offsetOf(object->getBeginLoc(), offset))
            m_dependent_objects[offset] = object;
        if (!object->isListInitialization())
            addDependentInit(Kind::Construct, object->getLParenLoc(), object->getTypeSourceInfo(), object,
                             object->getRParenLoc(), object->getNumArgs());
    }
}

void CloneConstantEvaluatedVisitor::visitBlocker(clang::Expr* expr) {
    // what a use at the same place in another instantiation can't be spelled over
    if (expr->isTypeDependent()) return;
    if (const auto* binary = llvm::dyn_cast<clang::BinaryOperator>(expr); binary && !m_operator_callees.count(binary))
        block(Kind::Operator, binary->getOperatorLoc());
    if (const auto* unary = llvm::dyn_cast<clang::UnaryOperator>(expr); unary && !m_operator_callees.count(unary))
        block(Kind::Operator, unary->getOperatorLoc());
    if (const auto* subscript = llvm::dyn_cast<clang::ArraySubscriptExpr>(expr))
        block(Kind::Operator, subscript->getRBracketLoc());
    if (const auto* call = llvm::dyn_cast<clang::CallExpr>(expr); call && !llvm::isa<clang::CXXOperatorCallExpr>(call))
        block(Kind::Operator, call->getRParenLoc());
    if (const auto* cast = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(expr);
        cast && cast->getCastKind() != clang::CK_ConstructorConversion)
        block(Kind::Construct, cast->getLParenLoc());
    if (const auto* list = llvm::dyn_cast<clang::InitListExpr>(expr)) block(Kind::Construct, list->getLBraceLoc());
    // an object isn't converted where an instantiation has a scalar
    if (expr->getType().isNull() || expr->getType()->isRecordType() || expr->hasPlaceholderType() ||
        llvm::isa<clang::ImplicitCastExpr, clang::FullExpr, clang::MaterializeTemporaryExpr, clang::CXXBindTemporaryExpr>(
            expr))
        return;
    if (const auto* call = llvm::dyn_cast<clang::CXXMemberCallExpr>(expr))
        if (const auto* member = llvm::dyn_cast<clang::MemberExpr>(call->getCallee()))
            if (member->getMemberLoc().isInvalid()) return;
    Range range;
    if (rangeOf(expr->getSourceRange(), range)) m_scalars.insert(range);
}

// ─── clones ───────────────────────────────────────────────────────

const clang::FunctionDecl* CloneConstantEvaluatedVisitor::patternOf(const clang::FunctionDecl* function) {
    if (const clang::FunctionDecl* pattern = function->getTemplateInstantiationPattern()) function = pattern;
    return function->getCanonicalDecl();
}

bool CloneConstantEvaluatedVisitor::isDefinedHere(const clang::FunctionDecl* pattern) const {
    const clang::FunctionDecl* definition = nullptr;
    unsigned offset;
    return pattern->hasBody(definition) && !definition->isDefaulted() && !definition->isDeleted() &&
           !pattern->isImplicit() && offsetOf(definition->getLocation(), offset);
}

std::string CloneConstantEvaluatedVisitor::cloneName(const clang::FunctionDecl* pattern) const {
    if (const auto* conversion = llvm::dyn_cast<clang::CXXConversionDecl>(pattern)) {
        std::string type = conversion->getConversionType().getAsString();
        for (char& c : type)
            if (!isalnum((unsigned char)c)) c = '_';
        return kPrefix + std::string("operator_") + type;
    }
    if (pattern->isOverloadedOperator())
        return kPrefix + std::string("operator_") + operatorName(pattern->getOverloadedOperator());
    if (pattern->getDeclName().isIdentifier()) return kPrefix + pattern->getName().str();
    return "";
}

bool CloneConstantEvaluatedVisitor::isCloned(const clang::FunctionDecl* pattern) const {
    if (!isDefinedHere(pattern) || llvm::isa<clang::CXXDestructorDecl>(pattern)) return false;
    // the target type of a conversion template is deduced from where it converts to
    if (llvm::isa<clang::CXXConversionDecl>(pattern) && pattern->getDescribedFunctionTemplate()) return false;
    return llvm::isa<clang::CXXConstructorDecl>(pattern) || !cloneName(pattern).empty();
}

void CloneConstantEvaluatedVisitor::reach(const clang::FunctionDecl* function) {
    const clang::FunctionDecl* pattern = patternOf(function);
    if (!m_reached.insert(pattern).second) return;
    // a clone of an explicit specialization specializes the clone of its template
    if (function->getTemplateSpecializationKind() == clang::TSK_ExplicitSpecialization)
        if (const clang::FunctionTemplateDecl* primary = function->getPrimaryTemplate())
            reach(primary->getTemplatedDecl());
    // a virtual call is dispatched to any of its overriders
    for (const clang::CXXMethodDecl* overrider : m_overriders)
        for (const clang::CXXMethodDecl* overridden : overrider->overridden_methods())
            if (patternOf(overridden) == pattern) reach(overrider);
    if (!isCloned(pattern)) return;
    for (const clang::FunctionDecl* redecl : pattern->redecls()) {
        Range range;
        if (declRange(redecl, range)) m_work.push_back(range);
    }
}

void CloneConstantEvaluatedVisitor::reachAll() {
    // the innermost constant context or unevaluated operand around each use
    struct Context {
        Range range;
        bool constant;
    };
    std::vector<Context> contexts;
    for (const Range& range : m_constant) contexts.push_back({range, true});
    for (const Range& range : m_unevaluated) contexts.push_back({range, false});
    std::sort(contexts.begin(), contexts.end(), [](const Context& a, const Context& b) {
        return a.range.first != b.range.first ? a.range.first < b.range.first : a.range.second > b.range.second;
    });
    std::vector<const Context*> innermost(m_uses.size(), nullptr);
    std::vector<const Context*> open;
    size_t next = 0;
    for (size_t i = 0; i < m_uses.size(); ++i) {
        unsigned offset = m_uses[i].offset;
        for (; next < contexts.size() && contexts[next].range.first <= offset; ++next) {
            while (!open.empty() && open.back()->range.second <= contexts[next].range.first) open.pop_back();
            open.push_back(&contexts[next]);
        }
        while (!open.empty() && open.back()->range.second <= offset) open.pop_back();
        if (!open.empty()) innermost[i] = open.back();
    }

    auto visit = [this](Use& use, char how) {
        use.reached = how;
        if (use.kind == Kind::Body)
            m_work.push_back(use.range);
        else if (use.function)
            reach(use.function);
        // a dependent name is any of the functions of its name
        else if (use.kind == Kind::Dependent)
            if (auto named = m_named.find(writtenName(use)); named != m_named.end() && !named->second.others)
                for (const clang::FunctionDecl* function : named->second.functions) reach(function);
    };
    for (size_t i = 0; i < m_uses.size(); ++i)
        if (innermost[i] && innermost[i]->constant) visit(m_uses[i], 1);
    while (!m_work.empty()) {
        Range range = m_work.back();
        m_work.pop_back();
        auto it = std::lower_bound(m_uses.begin(), m_uses.end(), range.first,
                                   [](const Use& use, unsigned offset) { return use.offset < offset; });
        for (size_t i = it - m_uses.begin(); i < m_uses.size() && m_uses[i].offset < range.second; ++i)
            if (!m_uses[i].reached && !(innermost[i] && innermost[i]->range.first >= range.first))
                visit(m_uses[i], 2);
    }
}

std::string CloneConstantEvaluatedVisitor::writtenName(const Use& use) const {
    return m_text.substr(use.range.first, use.range.second - use.range.first).str();
}

bool CloneConstantEvaluatedVisitor::isSpelledByName(const std::string& name) const {
    // only if whatever an instantiation finds by it has a clone
    auto named = m_named.find(name);
    return named != m_named.end() && !named->second.others &&
           llvm::all_of(named->second.functions, [this](const clang::FunctionDecl* function) { return isCloned(function); });
}

std::vector<CloneConstantEvaluatedVisitor::Edit> CloneConstantEvaluatedVisitor::spell(const Use& use) const {
    std::string name;
    if (use.kind == Kind::Dependent) {
        if (isSpelledByName(writtenName(use))) name = kPrefix + writtenName(use);
    } else if (isCloned(patternOf(use.function))) {
        name = cloneName(patternOf(use.function));
    }
    std::vector<Edit> edits;
    bool constructs = use.kind == Kind::Construct || use.kind == Kind::Converting;
    if (name.empty() && !(constructs && isCloned(patternOf(use.function)))) return edits;

    auto range = [this](const clang::Stmt* stmt, Range& out) { return stmt && rangeOf(stmt->getSourceRange(), out); };
    auto replace = [&edits](Range at, std::string text) {
        edits.push_back({at.first, at.second - at.first, std::move(text), kReplace});
    };
    // `T t = x` is spelled `T t = __invisible_clone_construct<T>(0, x)`, which is
    // a clone's construction for a class type and x itself for a scalar.
    if (auto init = m_dependent_inits.find({use.offset, use.kind}); constructs && init != m_dependent_inits.end()) {
        const auto* record = llvm::cast<clang::CXXConstructExpr>(use.expr)->getConstructor()->getParent();
        if (use.kind == Kind::Converting &&
            llvm::any_of(record->ctors(), [](const clang::CXXConstructorDecl* ctor) { return ctor->isExplicit(); }))
            return {};
        bool args = use.kind == Kind::Converting || init->second.args;
        unsigned at = use.kind == Kind::Construct ? use.offset + 1 : use.offset;
        edits.push_back({at, 0, std::string(kConstruct) + "<" + init->second.type + ">(0" + (args ? ", " : ""),
                         kBefore - kBefore / 2});
        edits.push_back({init->second.end, 0, ")", kBefore / 2});
        return edits;
    }
    switch (use.kind) {
    case Kind::Name:
    case Kind::Dependent:
        replace(use.range, name);
        break;
    case Kind::Conversion: {
        Range object;
        if (!range(use.expr, object)) break;
        if (m_scalars.count(object)) return {};
        long length = 2 * long(object.second - object.first) + 1;
        edits.push_back({object.first, 0, "(", kBefore - length});
        edits.push_back({object.second, 0, ")." + name + "()", length});
        break;
    }
    case Kind::Construct: {
        const auto* construct = llvm::cast<clang::CXXConstructExpr>(use.expr);
        bool args = construct->getNumArgs() && !llvm::isa<clang::CXXDefaultArgExpr>(construct->getArg(0));
        edits.push_back({use.offset + 1, 0, std::string(kTag) + "{}" + (args ? ", " : ""), kBefore - kBefore / 2});
        break;
    }
    case Kind::Converting: {
        // `W w = 1` is spelled `W w = W(__invisible_clone_tag{}, 1)`, which could
        // choose an explicit constructor copy initialization doesn't.
        const auto* construct = llvm::cast<clang::CXXConstructExpr>(use.expr);
        const clang::CXXRecordDecl* record = construct->getConstructor()->getParent();
        Range arg;
        if (record->isLocalClass() || !record->getIdentifier() || !range(construct->getArg(0), arg) ||
            llvm::any_of(record->ctors(), [](const clang::CXXConstructorDecl* ctor) { return ctor->isExplicit(); }))
            return {};
        clang::PrintingPolicy policy(m_ctx.getLangOpts());
        policy.FullyQualifiedName = true;
        policy.PrintAsCanonical = true;
        std::string type = construct->getType().getUnqualifiedType().getAsString(policy);
        long length = 2 * long(arg.second - arg.first) + 1;
        edits.push_back({arg.first, 0, type + "(" + kTag + "{}, ", kBefore - length});
        edits.push_back({arg.second, 0, ")", length});
        break;
    }
    case Kind::Operator: {
        // `a + b` is spelled `(a).__invisible_clone_operator_Plus(b)` for a member,
        // `__invisible_clone_operator_Plus(a, b)` otherwise.
        const auto* call = llvm::cast<clang::CXXOperatorCallExpr>(use.expr);
        bool member = llvm::isa<clang::CXXMethodDecl>(use.function);
        clang::OverloadedOperatorKind op = call->getOperator();
        Range whole, op_range, lhs, rhs;
        if (!range(call, whole) || !rangeOf(call->getOperatorLoc(), op_range) || !range(call->getArg(0), lhs)) break;
        long length = 2 * long(whole.second - whole.first);
        auto before = [&](unsigned at, std::string text) { edits.push_back({at, 0, std::move(text), kBefore - length}); };
        auto after = [&](unsigned at, std::string text) { edits.push_back({at, 0, std::move(text), length}); };
        // what opens the call, and what separates its object from its arguments
        std::string open = member ? "(" : name + "(";
        std::string call_args = member ? ")." + name + "(" : ", ";
        unsigned args = call->getNumArgs();
        bool postfix = args == 2 && lhs.second <= op_range.first && (op == clang::OO_PlusPlus || op == clang::OO_MinusMinus);
        if (member && op == clang::OO_Call) {
            before(lhs.first, open);
            after(lhs.second, ")." + name);
        } else if (member && op == clang::OO_Subscript) {
            size_t bracket = m_text.find_first_not_of(" \t\r\n", lhs.second);
            if (bracket == llvm::StringRef::npos || m_text[bracket] != '[') break;
            before(lhs.first, open);
            replace({lhs.second, unsigned(bracket) + 1}, call_args);
            replace(op_range, ")");
        } else if (member && op == clang::OO_Arrow) {
            before(lhs.first, open);
            replace({lhs.second, op_range.second}, ")." + name + "()->");
        } else if (args == 1) {
            replace({op_range.first, lhs.first}, open);
            after(lhs.second, member ? ")." + name + "()" : ")");
        } else if (postfix) {
            before(lhs.first, open);
            replace({lhs.second, op_range.second}, member ? ")." + name + "(0)" : ", 0)");
        } else if (args == 2 && lhs.second <= op_range.first && range(call->getArg(1), rhs)) {
            before(lhs.first, open);
            replace({lhs.second, rhs.first}, call_args);
            after(rhs.second, ")");
            auto rewritten = m_rewritten.find(call);
            if (rewritten != m_rewritten.end()) {
                edits.push_back({lhs.first, 0, rewritten->second.first, kBefore - length - 1});
                edits.push_back({rhs.second, 0, rewritten->second.second, length + 1});
            }
        }
        break;
    }
    default:
        break;
    }
    return edits;
}

void CloneConstantEvaluatedVisitor::keepUnspelled() {
    std::set<unsigned> spelled;
    for (const auto& [key, spelling] : m_spellings)
        if (!spelling.conflicts && !spelling.edits.empty()) spelled.insert(key.first);
    std::vector<const clang::FunctionDecl*> work;
    auto keep = [&](const clang::FunctionDecl* function) {
        const clang::FunctionDecl* pattern = patternOf(function);
        if (isDefinedHere(pattern) && m_kept.insert(pattern).second) work.push_back(pattern);
    };
    for (const Use& use : m_uses) {
        if (use.reached && use.kind == Kind::Dependent && !isSpelledByName(writtenName(use)))
            if (auto named = m_named.find(writtenName(use)); named != m_named.end())
                for (const clang::FunctionDecl* function : named->second.functions) keep(function);
        if (!use.reached || !use.function) continue;
        if (use.kind == Kind::Point) {
            if (!llvm::isa<clang::CXXConstructorDecl>(use.function) && !spelled.count(use.offset)) keep(use.function);
        } else if (use.kind == Kind::Unspelled) {
            keep(use.function);
        } else {
            auto spelling = m_spellings.find({use.offset, use.kind});
            if (spelling == m_spellings.end() || spelling->second.conflicts || spelling->second.edits.empty())
                keep(use.function);
        }
    }
    // what a kept function uses is kept as well
    while (!work.empty()) {
        const clang::FunctionDecl* pattern = work.back();
        work.pop_back();
        for (const clang::FunctionDecl* redecl : pattern->redecls()) {
            Range range;
            if (!declRange(redecl, range)) continue;
            auto it = std::lower_bound(m_uses.begin(), m_uses.end(), range.first,
                                       [](const Use& use, unsigned offset) { return use.offset < offset; });
            for (; it != m_uses.end() && it->offset < range.second; ++it)
                if (it->function) keep(it->function);
        }
    }
}

std::vector<CloneConstantEvaluatedVisitor::Edit> CloneConstantEvaluatedVisitor::editsWithin(Range range) const {
    std::vector<Edit> edits;
    for (const auto* list : {&m_edits, &m_clone_edits})
        for (const Edit& edit : *list)
            if (edit.offset >= range.first && edit.offset + edit.length <= range.second &&
                (edit.length || edit.offset < range.second))
                edits.push_back(edit);
    return edits;
}

std::string CloneConstantEvaluatedVisitor::memberInits(const clang::CXXConstructorDecl* ctor) const {
    // the members it doesn't initialize are initialized by their default
    // initializers, which are spelled in it if they evaluate clones.
    if (!ctor->doesThisDeclarationHaveABody() || ctor->isDelegatingConstructor()) return "";
    std::string inits;
    for (const clang::FieldDecl* field : ctor->getParent()->fields()) {
        Range range;
        const clang::Expr* init = field->getInClassInitializer();
        if (!field->getIdentifier() || !init || !rangeOf(init->getSourceRange(), range)) continue;
        if (llvm::any_of(ctor->inits(), [field](const clang::CXXCtorInitializer* written) {
                return written->isWritten() && written->getMember() == field;
            }))
            continue;
        if (llvm::none_of(m_clone_edits, [&range](const Edit& edit) {
                return edit.offset >= range.first && edit.offset < range.second;
            }))
            continue;
        std::string value = apply(range, editsWithin(range));
        inits += (inits.empty() ? "" : ", ") + field->getName().str() + (value[0] == '{' ? value : "(" + value + ")");
    }
    return inits;
}

std::string CloneConstantEvaluatedVisitor::cloneOf(const clang::FunctionDecl* redecl, Range range) const {
    std::vector<Edit> edits = editsWithin(range);
    if (const auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(redecl)) {
        unsigned paren;
        if (!offsetOf(ctor->getFunctionTypeLoc().getLParenLoc(), paren)) return "";
        edits.push_back({paren + 1, 0, std::string(kTag) + (ctor->getNumParams() ? ", " : ""), kBefore - kBefore / 2});
        std::string inits = memberInits(ctor);
        unsigned last = 0;
        for (const clang::CXXCtorInitializer* init : ctor->inits()) {
            Range written;
            if (init->isWritten() && rangeOf(init->getSourceRange(), written)) last = std::max(last, written.second);
        }
        unsigned body;
        if (!inits.empty() && last)
            edits.push_back({last, 0, ", " + inits, kBefore});
        else if (!inits.empty() && offsetOf(ctor->getBody()->getBeginLoc(), body))
            edits.push_back({body, 0, ": " + inits + " ", kBefore});
    } else {
        Range name;
        if (!rangeOf(redecl->getNameInfo().getSourceRange(), name)) return "";
        edits.push_back({name.first, name.second - name.first, cloneName(patternOf(redecl)), kReplace});
    }
    if (llvm::isa<clang::CXXConversionDecl>(redecl)) {
        // `operator T()` becomes `T __invisible_clone_operator_T()`, which can't be explicit
        Range type;
        unsigned begin;
        clang::TypeSourceInfo* info = redecl->getNameInfo().getNamedTypeInfo();
        clang::SourceLocation qualifier = redecl->getQualifierLoc().getBeginLoc();
        if (!info || !rangeOf(info->getTypeLoc().getSourceRange(), type) ||
            !offsetOf(qualifier.isValid() ? qualifier : redecl->getNameInfo().getBeginLoc(), begin))
            return "";
        edits.push_back({begin, 0, m_text.substr(type.first, type.second - type.first).str() + " ", kBefore});
        llvm::StringRef head = m_text.substr(range.first, begin - range.first);
        auto identifier = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
        for (size_t at = head.find("explicit"); at != llvm::StringRef::npos; at = head.find("explicit", at + 1))
            if ((!at || !identifier(head[at - 1])) && (at + 8 == head.size() || !identifier(head[at + 8])))
                edits.push_back({unsigned(range.first + at), 8, "", kReplace});
    }
    // a clone overrides the clones of what its original overrides, if they exist at all
    for (const clang::Attr* attr : redecl->attrs()) {
        Range attr_range;
        if ((llvm::isa<clang::OverrideAttr>(attr) || llvm::isa<clang::FinalAttr>(attr)) &&
            rangeOf(attr->getRange(), attr_range))
            edits.push_back({attr_range.first, attr_range.second - attr_range.first, "", kReplace});
    }
    return apply(range, edits);
}

std::string CloneConstantEvaluatedVisitor::apply(Range range, std::vector<Edit> edits) const {
    std::stable_sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) {
        return a.offset != b.offset ? a.offset < b.offset : a.order < b.order;
    });
    std::string out;
    unsigned at = range.first;
    for (size_t i = 0; i < edits.size(); ++i) {
        const Edit& edit = edits[i];
        if (edit.offset < at || edit.offset + edit.length > range.second) continue;
        if (i && edit.offset == edits[i - 1].offset && edit.length == edits[i - 1].length &&
            edit.text == edits[i - 1].text && edit.order == edits[i - 1].order)
            continue;
        out += m_text.substr(at, edit.offset - at);
        out += edit.text;
        at = edit.offset + edit.length;
    }
    out += m_text.substr(at, range.second - at);
    return out;
}

void CloneConstantEvaluatedVisitor::run() {
    TraverseDecl(m_ctx.getTranslationUnitDecl());
    std::stable_sort(m_uses.begin(), m_uses.end(), [](const Use& a, const Use& b) { return a.offset < b.offset; });
    // a conversion used during deduction is spelled at its point of instantiation
    for (Use& use : m_uses) {
        auto object = m_dependent_objects.find(use.offset);
        if (use.kind == Kind::Point && llvm::isa<clang::CXXConversionDecl>(use.function) &&
            object != m_dependent_objects.end()) {
            use.kind = Kind::Conversion;
            use.expr = object->second;
        }
    }
    reachAll();

    // the uses at one place in different instantiations are spelled alike, or not at all
    for (const Use& use : m_uses) {
        if (!use.reached || use.kind == Kind::Point || use.kind == Kind::Unspelled || use.kind == Kind::Body) continue;
        Key key{use.offset, use.kind == Kind::Dependent ? Kind::Name : use.kind};
        if (m_blocked.count(key) && !m_dependent_inits.count(key)) continue;
        std::vector<Edit> edits = spell(use);
        bool unsafe = (use.kind == Kind::Conversion || use.kind == Kind::Converting) && edits.empty() &&
                      isCloned(patternOf(use.function));
        auto [it, inserted] = m_spellings.insert({key, {edits, use.reached == 2, unsafe, !use.instantiated}});
        Spelling& spelling = it->second;
        spelling.in_pattern = spelling.in_pattern || !use.instantiated;
        if (inserted) continue;
        spelling.in_clone_only = spelling.in_clone_only && use.reached == 2;
        spelling.conflicts = spelling.conflicts || unsafe || spelling.edits.size() != edits.size() ||
                             !std::equal(edits.begin(), edits.end(), spelling.edits.begin(), [](const Edit& a, const Edit& b) {
                                 return a.offset == b.offset && a.length == b.length && a.text == b.text;
                             });
    }
    for (auto& [key, spelling] : m_spellings) {
        // the argument of a converting constructor is only converted in every
        // instantiation if its template converts it as well
        if (key.second == Kind::Converting && !spelling.in_pattern && !m_dependent_inits.count(key))
            spelling.conflicts = true;
        if (spelling.conflicts) continue;
        auto& list = spelling.in_clone_only ? m_clone_edits : m_edits;
        list.insert(list.end(), spelling.edits.begin(), spelling.edits.end());
    }
    keepUnspelled();

    // the clones of the functions in a function, eg. of the members of a local class,
    // are part of its clone
    std::vector<std::pair<Range, const clang::FunctionDecl*>> redecls;
    for (const clang::FunctionDecl* pattern : m_reached) {
        if (!isCloned(pattern)) continue;
        for (const clang::FunctionDecl* redecl : pattern->redecls()) {
            Range range;
            if (declRange(redecl, range)) redecls.push_back({range, redecl});
        }
    }
    std::stable_sort(redecls.begin(), redecls.end(), [](const auto& a, const auto& b) {
        return a.first.second - a.first.first < b.first.second - b.first.first;
    });
    for (const auto& [range, redecl] : redecls) {
        std::string clone = cloneOf(redecl, range);
        if (!clone.empty()) m_edits.push_back({range.second, 0, "\n" + clone, 0});
    }
    for (const clang::FunctionDecl* pattern : m_kept) {
        for (const clang::FunctionDecl* redecl : pattern->redecls()) {
            Range range;
            unsigned begin;
            if (!declRange(redecl, range) || !offsetOf(redecl->getInnerLocStart(), begin)) continue;
            // after the `[[...]]` attributes, which older compilers only accept first
            while (m_text.substr(begin).starts_with("[[")) {
                size_t close = m_text.find("]]", begin);
                if (close == llvm::StringRef::npos) break;
                begin = m_text.find_first_not_of(" \t\r\n", close + 2);
            }
            m_edits.push_back({begin, 0, kKept, kBefore});
        }
    }
    if (m_edits.empty()) return;
    auto uses = [this](const char* name) {
        return llvm::any_of(m_edits, [name](const Edit& edit) { return edit.text.find(name) != std::string::npos; });
    };
    if (uses(kConstruct))
        m_edits.push_back({0, 0,
                           "template <class T, class... A> constexpr auto " + std::string(kConstruct) +
                               "(int, A&&... a) -> decltype(T(" + kTag + "{}, static_cast<A&&>(a)...)) { return T(" +
                               kTag + "{}, static_cast<A&&>(a)...); }\n"
                               "template <class T, class... A> constexpr T " + kConstruct +
                               "(long, A&&... a) { return T(static_cast<A&&>(a)...); }\n",
                           -kReplace + 1});
    if (uses(kTag)) m_edits.push_back({0, 0, "struct " + std::string(kTag) + " {};\n", -kReplace});

    const auto& src_mgr = m_ctx.getSourceManager();
    clang::SourceLocation start = src_mgr.getLocForStartOfFile(src_mgr.getMainFileID());
    m_rewriter.ReplaceText(start, m_text.size(), apply({0, unsigned(m_text.size())}, m_edits));
}

}  // namespace invisible
