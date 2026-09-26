#include "preprocess/disassemble_cxx_for_range_stmt/DisassembleCXXForRangeStmtVisitor.h"
#include "helpers.h"

#include "clang/Lex/Lexer.h"

namespace invisible {

// ─── DisassembleCXXForRangeStmtVisitor ────────────────────────────

DisassembleCXXForRangeStmtVisitor::DisassembleCXXForRangeStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(m_ctx.getLangOpts()) {
    m_policy.adjustForCPlusPlus();
}

bool DisassembleCXXForRangeStmtVisitor::VisitCXXForRangeStmt(clang::CXXForRangeStmt* for_stmt) {
    /**
     * `for (init; decl : range) body`
     * is transformed into
     * {
     *   init;
     *   auto &&__for_range_#id = range;  [skipped when range names a variable]
     *   auto __for_begin_#id = begin-expr;
     *   auto __for_end_#id = end-expr;
     *   for (; __for_begin_#id != __for_end_#id; ++__for_begin_#id) {
     *     decl = *__for_begin_#id;
     *     body
     *   }
     * }
     */
    if (for_stmt->getBeginStmt() == nullptr) return true;
    auto& src_mgr = m_ctx.getSourceManager();
    const auto& lang_opts = m_ctx.getLangOpts();

    // the implicit variables are renamed so that the printed begin/end
    // expressions, condition and increment refer to the declared ones.
    std::string index = std::to_string(m_current_index++);
    std::string range_init = sourceText(clang::CharSourceRange::getTokenRange(for_stmt->getRangeInit()->getSourceRange()));
    bool names_variable = llvm::isa<clang::DeclRefExpr>(for_stmt->getRangeInit()->IgnoreParenImpCasts());
    std::string range = names_variable ? range_init : "__for_range_" + index;
    std::string begin = "__for_begin_" + index;
    std::string end = "__for_end_" + index;
    auto rename = [this](clang::DeclStmt* stmt, const std::string& name) {
        auto* var = llvm::cast<clang::VarDecl>(stmt->getSingleDecl());
        var->setDeclName(&m_ctx.Idents.get(name));
        return var;
    };
    rename(for_stmt->getRangeStmt(), range);
    clang::VarDecl* begin_var = rename(for_stmt->getBeginStmt(), begin);
    clang::VarDecl* end_var = rename(for_stmt->getEndStmt(), end);

    auto loop_var_begin = for_stmt->getLoopVarStmt()->getBeginLoc();
    std::string init;
    if (auto* init_stmt = for_stmt->getInit())
        init = sourceText(clang::CharSourceRange::getCharRange(init_stmt->getBeginLoc(), loop_var_begin)) + "\n";
    std::string loop_var = llvm::StringRef(sourceText(clang::CharSourceRange::getCharRange(loop_var_begin, for_stmt->getColonLoc()))).rtrim().str();

    std::string header = "{\n" + init;
    if (!names_variable) header += "auto &&" + range + " = " + range_init + ";\n";
    header += "auto " + begin + " = " + print(begin_var->getInit()) + ";\n" +
        "auto " + end + " = " + print(end_var->getInit()) + ";\n" +
        "for (; " + print(for_stmt->getCond()) + "; " + print(for_stmt->getInc()) + ") {\n" +
        loop_var + " = *" + begin + ";\n";

    // a body that is not a block ends with the semicolon after its last token
    clang::Stmt* body = for_stmt->getBody();
    clang::SourceLocation body_end = clang::Lexer::findLocationAfterToken(body->getEndLoc(), clang::tok::semi, src_mgr, lang_opts, false);
    if (body_end.isInvalid()) body_end = clang::Lexer::getLocForEndOfToken(body->getEndLoc(), 0, src_mgr, lang_opts);

    m_rewriter.ReplaceText(clang::CharSourceRange::getCharRange(for_stmt->getBeginLoc(), body->getBeginLoc()), header);
    m_rewriter.InsertTextAfter(body_end, "\n}\n}");
    return true;
}

std::string DisassembleCXXForRangeStmtVisitor::print(clang::Stmt* stmt) {
    std::string text;
    llvm::raw_string_ostream out(text);
    qualifyNonLocalNames(stmt, m_ctx);
    stmt->printPretty(out, nullptr, m_policy);
    return text;
}

std::string DisassembleCXXForRangeStmtVisitor::sourceText(clang::CharSourceRange range) {
    return clang::Lexer::getSourceText(range, m_ctx.getSourceManager(), m_ctx.getLangOpts()).str();
}

}  // namespace invisible
