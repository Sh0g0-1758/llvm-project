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

    std::string decl;
    llvm::raw_string_ostream decl_out(decl);

    if (glob_decl->isOutOfLine()) {
        if (glob_decl->isStaticDataMember()) {
            decl = "";
        } else {
            printQualifiedVarDecl(decl_out, glob_decl, m_deferred_policy_def, m_ctx);
        }
    } else {
        glob_decl->print(decl_out, m_policy);
    }

    decl_out.flush();

    m_rewriter.ReplaceText(glob_decl->getSourceRange(), decl);
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
