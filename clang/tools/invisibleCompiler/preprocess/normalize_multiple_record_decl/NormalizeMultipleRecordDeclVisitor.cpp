#include "preprocess/normalize_multiple_record_decl/NormalizeMultipleRecordDeclVisitor.h"
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

// ─── NormalizeMultipleRecordDeclVisitor ──────────────────────────────────────────────

NormalizeMultipleRecordDeclVisitor::NormalizeMultipleRecordDeclVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_deferred_policy_def(m_ctx.getLangOpts()), m_custom_printer(ctx, m_policy) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;

    m_deferred_policy_def = m_policy;
    m_deferred_policy_def.FullyQualifiedName = true;
}

bool NormalizeMultipleRecordDeclVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (func->isDefaulted() || func->isDeleted()) return true;
    if (isInsideFunction(func)) return true;
    if (!func->hasBody()) return true;
    if (!func->isThisDeclarationADefinition()) return true;

    std::string def;
    llvm::raw_string_ostream def_out(def);
    func->getBody()->printPretty(def_out, &m_custom_printer, m_policy);
    def_out.flush();

    m_rewriter.ReplaceText(func->getBody()->getSourceRange(), def);
    return true;
}

bool NormalizeMultipleRecordDeclVisitor::VisitVarDecl(clang::VarDecl* glob_decl) {
    if (glob_decl->isLocalVarDeclOrParm()) return true;
    if (isInsideFunction(glob_decl)) return true;
    if (glob_decl->getInstantiatedFromStaticDataMember()) return true;
    if (llvm::isa<clang::VarTemplateSpecializationDecl>(glob_decl)) return true;
    // the definition of a static data member declares nothing else
    if (glob_decl->isStaticDataMember() && glob_decl->isOutOfLine()) return true;
    // nor does the only declarator of a class it defines, whose members the printer
    // would print without the printer helpers, eg. dropping `if constexpr`
    if (const clang::TagDecl* tag = glob_decl->getType()->getAsTagDecl(); tag && tag->isEmbeddedInDeclarator()) {
        auto shares_declaration = [glob_decl](const clang::Decl* other) {
            return other != glob_decl && llvm::isa<clang::VarDecl>(other) && other->getBeginLoc() == glob_decl->getBeginLoc();
        };
        if (llvm::none_of(glob_decl->getDeclContext()->decls(), shares_declaration)) return true;
    }

    std::string decl;
    llvm::raw_string_ostream decl_out(decl);

    if (glob_decl->isOutOfLine()) {
        printQualifiedVarDecl(decl_out, glob_decl, m_deferred_policy_def, m_ctx);
    } else {
        glob_decl->print(decl_out, m_policy);
    }

    decl_out.flush();

    // the range of `T x(y)` of a scalar ends with `y`
    clang::SourceRange range = glob_decl->getSourceRange();
    if (glob_decl->getInitStyle() == clang::VarDecl::CallInit) {
        std::optional<clang::Token> next =
            clang::Lexer::findNextToken(range.getEnd(), m_ctx.getSourceManager(), m_ctx.getLangOpts());
        if (next && next->is(clang::tok::r_paren)) range.setEnd(next->getLocation());
    }
    m_rewriter.ReplaceText(range, decl);
    return true;
}

bool NormalizeMultipleRecordDeclVisitor::VisitFieldDecl(clang::FieldDecl* field_decl) {
    if (!field_decl->getType()->isDependentType()) return true;
    if (isInsideFunction(field_decl)) return true;

    std::string decl;
    llvm::raw_string_ostream decl_out(decl);
    field_decl->print(decl_out, m_policy);
    decl_out.flush();

    m_rewriter.ReplaceText(field_decl->getSourceRange(), decl);
    return true;
}

}  // namespace invisible
