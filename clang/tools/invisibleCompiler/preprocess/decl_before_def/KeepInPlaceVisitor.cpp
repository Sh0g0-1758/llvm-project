#include "preprocess/decl_before_def/KeepInPlaceVisitor.h"
#include "preprocess/decl_before_def/DeclBeforeDefVisitor.h"
#include "helpers.h"

#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <algorithm>

namespace invisible {

namespace {

// the function @p stmt names, eg. `f` of `f(x)` or `size` of `v.size()`
const clang::FunctionDecl* namedFunction(const clang::Stmt* stmt) {
    if (const auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt)) return llvm::dyn_cast<clang::FunctionDecl>(ref->getDecl());
    if (const auto* member = llvm::dyn_cast<clang::MemberExpr>(stmt))
        return llvm::dyn_cast<clang::FunctionDecl>(member->getMemberDecl());
    return nullptr;
}

}  // namespace

// --- KeepInPlaceVisitor -------------------------------------------

KeepInPlaceVisitor::KeepInPlaceVisitor(clang::ASTContext& ctx, const std::set<std::string>& names,
                                       const std::set<std::string>& dropped)
    : m_ctx(ctx), m_names(names), m_dropped(dropped), m_mono(ctx.getLangOpts().CPlusPlus) {
    const auto& src_mgr = ctx.getSourceManager();
    m_text = src_mgr.getBufferData(src_mgr.getMainFileID());
}

// eg. a use inside a macro's expansion is at the macro's name, where the file uses the macro
bool KeepInPlaceVisitor::offsetOf(clang::SourceLocation loc, unsigned& offset) const {
    const auto& src_mgr = m_ctx.getSourceManager();
    loc = src_mgr.getExpansionLoc(loc);
    if (loc.isInvalid() || !src_mgr.isInMainFile(loc)) return false;
    offset = src_mgr.getFileOffset(loc);
    return true;
}

// to the end of the last token, eg. past the `)` of `f(x)`
bool KeepInPlaceVisitor::rangeOf(clang::SourceRange range, Range& out) const {
    const auto& src_mgr = m_ctx.getSourceManager();
    clang::SourceLocation end =
        clang::Lexer::getLocForEndOfToken(src_mgr.getExpansionLoc(range.getEnd()), 0, src_mgr, m_ctx.getLangOpts());
    return offsetOf(range.getBegin(), out.first) && offsetOf(end, out.second) && out.first < out.second;
}

// What a kept declaration uses, eg. in its default arguments or noexcept, is kept too, so the
// range must cover the whole text. clang's ranges miss some of it, which is added back below.
bool KeepInPlaceVisitor::declRange(const clang::FunctionDecl* redecl, Range& out) const {
    // a declaration of a specialization that clang made during deduction is located at its
    // template. It isn't written, so it has no range.
    const clang::FunctionTemplateDecl* primary = redecl->getPrimaryTemplate();
    if (primary && redecl->getBeginLoc() == primary->getTemplatedDecl()->getBeginLoc()) return false;
    // likewise a member clang declared for an instantiated class is located at the member of the
    // class template
    const clang::FunctionDecl* member = redecl->getInstantiatedFromMemberFunction();
    if (member && redecl->getBeginLoc() == member->getBeginLoc()) return false;
    const clang::Decl* decl = redecl;
    // an abbreviated function template, eg. `void f(auto x)`, has no template head
    if (const auto* tmpl = redecl->getDescribedFunctionTemplate(); tmpl && tmpl->getBeginLoc().isValid()) decl = tmpl;
    if (!rangeOf(decl->getSourceRange(), out)) return false;
    // an out-of-line member template starts with the template heads of its class
    if (unsigned outer; offsetOf(redecl->getOuterLocStart(), outer)) out.first = std::min(out.first, outer);
    if (redecl->doesThisDeclarationHaveABody()) return true;
    // the range of a declaration can stop at its name, before a trailing return type or a requires
    // clause, eg. `auto f() -> int requires C<int>;`, so it is extended over both
    auto extend = [&](clang::SourceRange range) {
        if (Range tail; rangeOf(range, tail)) out.second = std::max(out.second, tail.second);
    };
    if (const clang::Expr* requires_clause = redecl->getTrailingRequiresClause().ConstraintExpr)
        extend(requires_clause->getSourceRange());
    if (const clang::TypeSourceInfo* type = redecl->getTypeSourceInfo()) extend(type->getTypeLoc().getSourceRange());
    size_t semi = m_text.find_first_not_of(" \t\r\n", out.second);
    if (semi != llvm::StringRef::npos && m_text[semi] == ';') out.second = semi + 1;
    return true;
}

void KeepInPlaceVisitor::addUse(clang::SourceLocation loc, const clang::FunctionDecl* function) {
    unsigned offset;
    if (function && offsetOf(loc, offset)) m_uses.push_back({offset, function});
}

// --- collection ---------------------------------------------------

// Keeps the seeds: the functions that must stay as written wherever they are used. Also records
// the range of every body. Outside a constant expression, a use inside a body keeps nothing, as
// the body moves to the end, after the declarations of everything it calls.
bool KeepInPlaceVisitor::VisitDecl(clang::Decl* decl) {
    // what a constant expression calls is evaluated where it is written, also in a body
    if (const auto* var = llvm::dyn_cast<clang::VarDecl>(decl); var && var->isConstexpr()) keepUsedIn(var->getInit());
    if (const auto* assertion = llvm::dyn_cast<clang::StaticAssertDecl>(decl); assertion && !m_in_body_assertion)
        keepUsedIn(assertion->getAssertExpr());
    auto* function = llvm::dyn_cast<clang::FunctionDecl>(decl);
    if (!function) return true;
    // an explicit instantiation definition, eg. `template void Data::reserve<true>(int);`, needs
    // the template's definition where it is written, and can't be followed by a specialization
    if (function->getTemplateSpecializationKind() == clang::TSK_ExplicitInstantiationDefinition) keep(function, true, true);
    // and a constexpr function that isn't a template: any constant expression may call it, also
    // through a concept or argument dependent lookup, and these calls can't all be found. The
    // moved code calls its copy instead.
    if (m_mono && function->isConstexpr() && !function->isTemplated() && !isInstantiatedCopy(function) &&
        function->doesThisDeclarationHaveABody())
        keep(function, true, true);
    // and a function of a type, or a member of a class, that can't be named elsewhere, eg. of a
    // return type deduced to name the closure type of a lambda, can't be declared at the end
    if (!function->isDependentContext() && function->doesThisDeclarationHaveABody() && !isNamedAnywhere(function))
        keep(function, true, true);
    if (m_mono && !llvm::isa<clang::CXXDeductionGuideDecl>(function)) {
        // the functions the driver found must stay as written, see laterUnmoved. A used
        // instantiation whose copy didn't compile keeps its template. One that can't be copied is
        // specialized, or keeps its template if it can't be specialized either, eg. one returning
        // a local class
        std::string key = passKey(function);
        if (m_names.count(key)) {
            keep(function, true, true);
        } else if (isInstantiatedCopy(function) && function->isUsed() && !isAlwaysSpecialized(function)) {
            if (m_dropped.count(key)) keep(function, true);
            else if (!isCopyable(function)) specialize(function);
        }
        // clang generates the body of an implicit or defaulted member, eg. a defaulted copy
        // constructor. That body isn't in the text, so its calls can't be redirected to copies,
        // and their callees are specialized instead
        if ((function->isImplicit() || function->isDefaulted()) && function->doesThisDeclarationHaveABody()) {
            specializeUsedIn(function->getBody());
            if (const auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(function))
                for (const clang::CXXCtorInitializer* init : ctor->inits()) specializeUsedIn(init->getInit());
        }
    }
    // RecursiveASTVisitor doesn't traverse explicit specifiers
    if (clang::Expr* explicit_expr = clang::ExplicitSpecifier::getFromDecl(function).getExpr())
        TraverseStmt(explicit_expr);
    if (function->isTemplateInstantiation()) {
        // a virtual function is instantiated where its vtable is needed, eg. at a constructor,
        // which isn't a use of the function
        if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(function); !method || !method->isVirtual())
            addUse(function->getPointOfInstantiation(), function);
        // the traversal doesn't reach the noexcept expression an instantiation computed
        if (const auto* proto = function->getType()->getAs<clang::FunctionProtoType>();
            proto && clang::isComputedNoexcept(proto->getExceptionSpecType()))
            TraverseStmt(proto->getNoexceptExpr());
    }
    // the range of the body, starting at a constructor's written member initializers, eg.
    // `: data(alloc(n))`
    unsigned begin, end;
    if (function->doesThisDeclarationHaveABody() && offsetOf(function->getBody()->getBeginLoc(), begin) &&
        offsetOf(function->getBody()->getEndLoc(), end)) {
        if (const auto* ctor = llvm::dyn_cast<clang::CXXConstructorDecl>(function))
            for (const clang::CXXCtorInitializer* init : ctor->inits())
                if (unsigned at; init->isWritten() && offsetOf(init->getSourceLocation(), at)) begin = std::min(begin, at);
        m_bodies.push_back({begin, end + 1});
    }
    return true;
}

// What a static_assert inside a body calls isn't kept as a seed. A moved body prints the
// assertion as `;`. A body left in place is scanned by run(), which keeps what it uses.
bool KeepInPlaceVisitor::TraverseStaticAssertDecl(clang::StaticAssertDecl* assertion) {
    bool outer = m_in_body_assertion;
    m_in_body_assertion = isInsideFunction(assertion);
    bool result = RecursiveASTVisitor::TraverseStaticAssertDecl(assertion);
    m_in_body_assertion = outer;
    return result;
}

// Keeps what a constant expression calls, eg. `size` in `std::array<int, size(xs)> a;` must be
// defined there. Also records every reference to a function as a use; run() keeps the uses
// outside the bodies.
bool KeepInPlaceVisitor::VisitStmt(clang::Stmt* stmt) {
    if (auto* constant = llvm::dyn_cast<clang::ConstantExpr>(stmt); constant && !m_in_body_assertion)
        keepUsedIn(constant->getSubExpr());
    if (m_mono) {
        if (auto* call = llvm::dyn_cast<clang::CallExpr>(stmt)) m_callees.insert(call->getCallee()->IgnoreParenImpCasts());
        // a member function named but not called, eg. its address taken, can't be redirected to a
        // copy, so it is specialized. The address of a free function becomes that of its copy.
        if (auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt); ref && !m_callees.count(ref))
            if (auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(ref->getDecl()); method && method->isInstance())
                specialize(method);
    }
    if (auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt)) {
        addUse(ref->getLocation(), llvm::dyn_cast<clang::FunctionDecl>(ref->getDecl()));
    } else if (auto* member = llvm::dyn_cast<clang::MemberExpr>(stmt)) {
        // an implicit conversion has no member name written, so the use is at its object
        clang::SourceLocation loc = member->getMemberLoc().isValid() ? member->getMemberLoc() : member->getBase()->getBeginLoc();
        addUse(loc, llvm::dyn_cast<clang::FunctionDecl>(member->getMemberDecl()));
    // a constructor and a destructor are used with no name written, eg. `Pt p{1, 2};` and `}`
    } else if (auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(stmt)) {
        addUse(construct->getBeginLoc(), construct->getConstructor());
    } else if (auto* temporary = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(stmt)) {
        addUse(temporary->getBeginLoc(), temporary->getTemporary()->getDestructor());
    }
    return true;
}

// --- keep ---------------------------------------------------------

// eg. `auto View<__invisible_lambda_1a2b3c4d5e_3>::begin()` can't be declared at the end of the TU:
// the lambda's struct is local to a function, so it can't be named there
bool KeepInPlaceVisitor::isNamedAnywhere(const clang::FunctionDecl* function) const {
    const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
    return isNameableAnywhere(function->getType()) &&
           (!method || isNameableAnywhere(m_ctx.getCanonicalTagType(method->getParent())));
}

const clang::FunctionDecl* KeepInPlaceVisitor::patternOf(const clang::FunctionDecl* function) {
    if (const clang::FunctionDecl* pattern = function->getTemplateInstantiationPattern()) function = pattern;
    return function->getCanonicalDecl();
}

// a defaulted, deleted or implicit function has no text to keep, nor one defined outside the main file
bool KeepInPlaceVisitor::isDefinedHere(const clang::FunctionDecl* pattern) const {
    const clang::FunctionDecl* definition = nullptr;
    unsigned offset;
    // the call operator of a lambda is written in the lambda expression
    if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(pattern); method && method->getParent()->isLambda())
        return false;
    return pattern->hasBody(definition) && !definition->isDefaulted() && !definition->isDeleted() &&
           !pattern->isImplicit() && offsetOf(definition->getLocation(), offset);
}

// Keeps the template, not the instantiation: `twice<int>` used outside a body keeps
// `template <class T> T twice(T)`. The text of each of its declarations is queued, and run()
// keeps what that text uses.
void KeepInPlaceVisitor::keep(const clang::FunctionDecl* function, bool templates, bool seed) {
    const clang::FunctionDecl* pattern = patternOf(function);
    if (templates && !seed && !pattern->isTemplated() && !pattern->isConstexpr()) return;
    // monomorphization specializes a constructor, a destructor or a virtual function, so its template
    // isn't kept. If one is used before its specialization, the driver keeps it on the next attempt.
    if (m_mono && !seed && isInstantiatedCopy(function) && isAlwaysSpecialized(function)) return;
    if (!m_kept.insert(pattern).second || !isDefinedHere(pattern)) return;
    for (const clang::FunctionDecl* redecl : pattern->redecls()) {
        Range range;
        if (declRange(redecl, range)) m_work.push_back({range, templates});
    }
}

// eg. `&Vec<int>::size` must stay a pointer to member, which a free function `size__mono...` can't
// be. So `Vec<int>::size` is written as an explicit specialization instead of a copy.
void KeepInPlaceVisitor::specialize(const clang::FunctionDecl* function) {
    if (!isInstantiatedCopy(function)) return;
    if (isCopyable(function, true)) m_specialized.insert(function->getCanonicalDecl());
    else keep(function, true);
}

// eg. a defaulted `bool operator==(const Box&) const = default;` calls the `operator==` of each
// member, eg. `Item<int>::operator==`, in a body clang generates. That body isn't in the text, so
// the call can't be redirected to a copy.
void KeepInPlaceVisitor::specializeUsedIn(const clang::Stmt* stmt) {
    if (!stmt) return;
    if (const clang::FunctionDecl* function = namedFunction(stmt)) specialize(function);
    for (const clang::Stmt* child : stmt->children()) specializeUsedIn(child);
}

// the compiler evaluates a constant expression where it is written, so what it uses must be
// defined there
void KeepInPlaceVisitor::keepUsedIn(const clang::Stmt* stmt) {
    if (!stmt) return;
    if (const clang::FunctionDecl* function = namedFunction(stmt)) keep(function, true);
    if (const auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(stmt)) keep(construct->getConstructor(), true);
    for (const clang::Stmt* child : stmt->children()) keepUsedIn(child);
}

// Collects the uses and the bodies, keeps what is used outside the bodies, then keeps what the
// kept text uses, until nothing new is queued. A nested body, eg. of a member of a local class, is
// merged into the body around it, so the bodies don't overlap and can be binary searched.
std::set<const clang::FunctionDecl*> KeepInPlaceVisitor::run() {
    TraverseDecl(m_ctx.getTranslationUnitDecl());
    std::sort(m_uses.begin(), m_uses.end(), [](const Use& a, const Use& b) { return a.offset < b.offset; });
    std::sort(m_bodies.begin(), m_bodies.end());
    std::vector<Range> bodies;
    for (const Range& body : m_bodies) {
        if (!bodies.empty() && body.first < bodies.back().second) bodies.back().second = std::max(bodies.back().second, body.second);
        else bodies.push_back(body);
    }
    auto in_body = [&bodies](unsigned offset) {
        auto it = std::upper_bound(bodies.begin(), bodies.end(), offset,
                                   [](unsigned at, const Range& body) { return at < body.first; });
        return it != bodies.begin() && offset < std::prev(it)->second;
    };
    // a use outside the bodies keeps templates and constexpr functions, and any function with a
    // deduced return type, eg. `decltype(f())` of `auto f()` needs the body of `f`
    for (const Use& use : m_uses)
        if (!in_body(use.offset))
            keep(use.function, !use.function->getDeclaredReturnType()->getContainedDeducedType());
    // and what a kept function uses
    while (!m_work.empty()) {
        auto [range, templates] = m_work.back();
        m_work.pop_back();
        auto it = std::lower_bound(m_uses.begin(), m_uses.end(), range.first,
                                   [](const Use& use, unsigned offset) { return use.offset < offset; });
        for (; it != m_uses.end() && it->offset < range.second; ++it) keep(it->function, templates);
    }
    return m_kept;
}

}  // namespace invisible
