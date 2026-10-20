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

// eg. a use in a macro's expansion is at the macro's name, where it is written in the file
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

// The uses inside a kept declaration, eg. those of its default arguments or noexcept, are kept too,
// so its whole text counts; clang's ranges miss some of it, which is added back below.
bool KeepInPlaceVisitor::declRange(const clang::FunctionDecl* redecl, Range& out) const {
    // deduction declares a specialization at its template before it is explicitly specialized
    const clang::FunctionTemplateDecl* primary = redecl->getPrimaryTemplate();
    if (primary && redecl->getBeginLoc() == primary->getTemplatedDecl()->getBeginLoc()) return false;
    // and a member of a class template specialization is declared at its template
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

// The seeds: what must stay as written whatever uses it. And the bodies, inside which a use doesn't
// count, since a body is moved to the end with what it calls declared before it.
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
    // and a constexpr function that isn't a template, which a constant
    // expression anywhere may evaluate, also through a concept or argument dependent lookup, which
    // can't all be seen; it is given a copy for what is deferred to call
    if (m_mono && function->isConstexpr() && !function->isTemplated() && !isInstantiatedCopy(function) &&
        function->doesThisDeclarationHaveABody())
        keep(function, true, true);
    // and a function of a type, or a member of a class, that can't be named elsewhere, eg. of a
    // return type deduced to name the closure type of a lambda, can't be declared at the end
    if (!function->isDependentContext() && function->doesThisDeclarationHaveABody() && !isNamedAnywhere(function))
        keep(function, true, true);
    if (m_mono && !llvm::isa<clang::CXXDeductionGuideDecl>(function)) {
        // what the driver learned must stay, see laterUnmoved. A used instantiation without a copy
        // keeps its template when its copy didn't compile, and is specialized when it can't be
        // copied, eg. one returning a local class
        std::string key = passKey(function);
        if (m_names.count(key)) {
            keep(function, true, true);
        } else if (isInstantiatedCopy(function) && function->isUsed() && !isAlwaysSpecialized(function)) {
            if (m_dropped.count(key)) keep(function, true);
            else if (!isCopyable(function)) specialize(function);
        }
        // an implicit member calls what it calls by name
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
        // a virtual function is instantiated where its vtable is, eg. at a constructor, not used there
        if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(function); !method || !method->isVirtual())
            addUse(function->getPointOfInstantiation(), function);
        // nor the exception specification an instantiation evaluated
        if (const auto* proto = function->getType()->getAs<clang::FunctionProtoType>();
            proto && clang::isComputedNoexcept(proto->getExceptionSpecType()))
            TraverseStmt(proto->getNoexceptExpr());
    }
    // the body, from the member initializers of a constructor, eg. `: data(alloc(n))`, on
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

// an assertion in a body isn't printed with what is deferred, and stays with a body that stays
bool KeepInPlaceVisitor::TraverseStaticAssertDecl(clang::StaticAssertDecl* assertion) {
    bool outer = m_in_body_assertion;
    m_in_body_assertion = isInsideFunction(assertion);
    bool result = RecursiveASTVisitor::TraverseStaticAssertDecl(assertion);
    m_in_body_assertion = outer;
    return result;
}

// eg. in `std::array<int, size(xs)> a;` the template argument is a ConstantExpr, and `size` must be
// defined there; every reference to a function is a use, kept if it is outside a body
bool KeepInPlaceVisitor::VisitStmt(clang::Stmt* stmt) {
    if (auto* constant = llvm::dyn_cast<clang::ConstantExpr>(stmt); constant && !m_in_body_assertion)
        keepUsedIn(constant->getSubExpr());
    if (m_mono) {
        if (auto* call = llvm::dyn_cast<clang::CallExpr>(stmt)) m_callees.insert(call->getCallee()->IgnoreParenImpCasts());
        // a member function whose address is taken is named, not called, that of another function is the copy's
        if (auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt); ref && !m_callees.count(ref))
            if (auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(ref->getDecl()); method && method->isInstance())
                specialize(method);
    }
    if (auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt)) {
        addUse(ref->getLocation(), llvm::dyn_cast<clang::FunctionDecl>(ref->getDecl()));
    } else if (auto* member = llvm::dyn_cast<clang::MemberExpr>(stmt)) {
        // an implicit conversion is called on its object
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

// eg. `auto View<__invisible_lambda_3>::begin()` can't be declared at the end, where the lambda's
// struct is local to a function
bool KeepInPlaceVisitor::isNamedAnywhere(const clang::FunctionDecl* function) const {
    const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
    return isNameableAnywhere(function->getType()) &&
           (!method || isNameableAnywhere(m_ctx.getCanonicalTagType(method->getParent())));
}

const clang::FunctionDecl* KeepInPlaceVisitor::patternOf(const clang::FunctionDecl* function) {
    if (const clang::FunctionDecl* pattern = function->getTemplateInstantiationPattern()) function = pattern;
    return function->getCanonicalDecl();
}

// a defaulted, deleted or implicit function has no text to keep, nor one of a header the TU
// didn't include in the main file
bool KeepInPlaceVisitor::isDefinedHere(const clang::FunctionDecl* pattern) const {
    const clang::FunctionDecl* definition = nullptr;
    unsigned offset;
    // the call operator of a lambda is written in the lambda expression
    if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(pattern); method && method->getParent()->isLambda())
        return false;
    return pattern->hasBody(definition) && !definition->isDefaulted() && !definition->isDeleted() &&
           !pattern->isImplicit() && offsetOf(definition->getLocation(), offset);
}

// Keeping is by pattern: `twice<int>` used outside a body keeps `template <class T> T twice(T)`,
// all of its declarations, each of whose text is then scanned for further uses in run().
void KeepInPlaceVisitor::keep(const clang::FunctionDecl* function, bool templates, bool seed) {
    const clang::FunctionDecl* pattern = patternOf(function);
    if (templates && !seed && !pattern->isTemplated() && !pattern->isConstexpr()) return;
    // monomorphization specializes a constructor, a destructor or a virtual function, which is then
    // defined; one used before it can be specialized is told by the error and kept then
    if (m_mono && !seed && isInstantiatedCopy(function) && isAlwaysSpecialized(function)) return;
    if (!m_kept.insert(pattern).second || !isDefinedHere(pattern)) return;
    for (const clang::FunctionDecl* redecl : pattern->redecls()) {
        Range range;
        if (declRange(redecl, range)) m_work.push_back({range, templates});
    }
}

// eg. `&Vec<int>::size` stays the member's address, so `Vec<int>::size` is written as an explicit
// specialization rather than given a copy `size__mono...`, which the address wouldn't be
void KeepInPlaceVisitor::specialize(const clang::FunctionDecl* function) {
    if (!isInstantiatedCopy(function)) return;
    if (isCopyable(function, true)) m_specialized.insert(function->getCanonicalDecl());
    else keep(function, true);
}

// eg. a defaulted `bool operator==(const Box&) const = default;` calls the members' own, eg.
// `Item<int>::operator==`, in a body clang made, which no redirection to a copy reaches
void KeepInPlaceVisitor::specializeUsedIn(const clang::Stmt* stmt) {
    if (!stmt) return;
    if (const clang::FunctionDecl* function = namedFunction(stmt)) specialize(function);
    for (const clang::Stmt* child : stmt->children()) specializeUsedIn(child);
}

// what a constant expression uses is evaluated by the compiler
void KeepInPlaceVisitor::keepUsedIn(const clang::Stmt* stmt) {
    if (!stmt) return;
    if (const clang::FunctionDecl* function = namedFunction(stmt)) keep(function, true);
    if (const auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(stmt)) keep(construct->getConstructor(), true);
    for (const clang::Stmt* child : stmt->children()) keepUsedIn(child);
}

// collect, then keep what is used outside the bodies (merged so that a nested body, eg. a local
// class's member, doesn't split one), then what the kept text uses, until nothing new is queued
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
    for (const Use& use : m_uses)
        if (!in_body(use.offset)) keep(use.function);
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
