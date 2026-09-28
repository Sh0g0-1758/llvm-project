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
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()), m_custom_printer(ctx, m_policy) {
    if (m_ctx.getLangOpts().CPlusPlus)
        m_policy.adjustForCPlusPlus();
    m_policy.IncludeTagDefinition = true;
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

}  // namespace invisible
