#include "preprocess/decl_before_def/KeepInPlaceVisitor.h"

#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <algorithm>

namespace invisible {

// ─── KeepInPlaceVisitor ───────────────────────────────────────────

KeepInPlaceVisitor::KeepInPlaceVisitor(clang::ASTContext& ctx) : m_ctx(ctx) {
    const auto& src_mgr = ctx.getSourceManager();
    m_text = src_mgr.getBufferData(src_mgr.getMainFileID());
}

bool KeepInPlaceVisitor::offsetOf(clang::SourceLocation loc, unsigned& offset) const {
    const auto& src_mgr = m_ctx.getSourceManager();
    loc = src_mgr.getExpansionLoc(loc);
    if (loc.isInvalid() || !src_mgr.isInMainFile(loc)) return false;
    offset = src_mgr.getFileOffset(loc);
    return true;
}

bool KeepInPlaceVisitor::rangeOf(clang::SourceRange range, Range& out) const {
    const auto& src_mgr = m_ctx.getSourceManager();
    clang::SourceLocation end =
        clang::Lexer::getLocForEndOfToken(src_mgr.getExpansionLoc(range.getEnd()), 0, src_mgr, m_ctx.getLangOpts());
    return offsetOf(range.getBegin(), out.first) && offsetOf(end, out.second) && out.first < out.second;
}

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
    // the range of a declaration stops at its name when it has a trailing return type
    for (const clang::Stmt* end : {static_cast<const clang::Stmt*>(redecl->getTrailingRequiresClause().ConstraintExpr)})
        if (Range tail; end && rangeOf(end->getSourceRange(), tail)) out.second = std::max(out.second, tail.second);
    if (const clang::TypeSourceInfo* type = redecl->getTypeSourceInfo())
        if (Range tail; rangeOf(type->getTypeLoc().getSourceRange(), tail)) out.second = std::max(out.second, tail.second);
    size_t semi = m_text.find_first_not_of(" \t\r\n", out.second);
    if (semi != llvm::StringRef::npos && m_text[semi] == ';') out.second = semi + 1;
    return true;
}

void KeepInPlaceVisitor::addUse(clang::SourceLocation loc, const clang::FunctionDecl* function) {
    unsigned offset;
    if (function && offsetOf(loc, offset)) m_uses.push_back({offset, function});
}

// ─── collection ───────────────────────────────────────────────────

bool KeepInPlaceVisitor::VisitDecl(clang::Decl* decl) {
    auto* function = llvm::dyn_cast<clang::FunctionDecl>(decl);
    if (!function) return true;
    // RecursiveASTVisitor doesn't traverse explicit specifiers
    if (clang::Expr* explicit_expr = clang::ExplicitSpecifier::getFromDecl(function).getExpr())
        TraverseStmt(explicit_expr);
    if (function->isTemplateInstantiation()) {
        addUse(function->getPointOfInstantiation(), function);
        // nor the exception specification an instantiation evaluated
        if (const auto* proto = function->getType()->getAs<clang::FunctionProtoType>();
            proto && clang::isComputedNoexcept(proto->getExceptionSpecType()))
            TraverseStmt(proto->getNoexceptExpr());
    }
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

bool KeepInPlaceVisitor::VisitStmt(clang::Stmt* stmt) {
    if (auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt)) {
        addUse(ref->getLocation(), llvm::dyn_cast<clang::FunctionDecl>(ref->getDecl()));
    } else if (auto* member = llvm::dyn_cast<clang::MemberExpr>(stmt)) {
        // an implicit conversion is called on its object
        clang::SourceLocation loc = member->getMemberLoc().isValid() ? member->getMemberLoc() : member->getBase()->getBeginLoc();
        addUse(loc, llvm::dyn_cast<clang::FunctionDecl>(member->getMemberDecl()));
    } else if (auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(stmt)) {
        addUse(construct->getBeginLoc(), construct->getConstructor());
    } else if (auto* temporary = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(stmt)) {
        addUse(temporary->getBeginLoc(), temporary->getTemporary()->getDestructor());
    }
    return true;
}

// ─── keep ─────────────────────────────────────────────────────────

const clang::FunctionDecl* KeepInPlaceVisitor::patternOf(const clang::FunctionDecl* function) {
    if (const clang::FunctionDecl* pattern = function->getTemplateInstantiationPattern()) function = pattern;
    return function->getCanonicalDecl();
}

bool KeepInPlaceVisitor::isDefinedHere(const clang::FunctionDecl* pattern) const {
    const clang::FunctionDecl* definition = nullptr;
    unsigned offset;
    // the call operator of a lambda is written in the lambda expression
    if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(pattern); method && method->getParent()->isLambda())
        return false;
    return pattern->hasBody(definition) && !definition->isDefaulted() && !definition->isDeleted() &&
           !pattern->isImplicit() && offsetOf(definition->getLocation(), offset);
}

void KeepInPlaceVisitor::keep(const clang::FunctionDecl* function) {
    const clang::FunctionDecl* pattern = patternOf(function);
    if (!m_kept.insert(pattern).second || !isDefinedHere(pattern)) return;
    for (const clang::FunctionDecl* redecl : pattern->redecls()) {
        Range range;
        if (declRange(redecl, range)) m_work.push_back(range);
    }
}

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
        Range range = m_work.back();
        m_work.pop_back();
        auto it = std::lower_bound(m_uses.begin(), m_uses.end(), range.first,
                                   [](const Use& use, unsigned offset) { return use.offset < offset; });
        for (; it != m_uses.end() && it->offset < range.second; ++it) keep(it->function);
    }
    return m_kept;
}

}  // namespace invisible
