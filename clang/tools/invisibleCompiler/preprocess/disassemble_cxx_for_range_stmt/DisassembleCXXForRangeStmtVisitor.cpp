#include "preprocess/disassemble_cxx_for_range_stmt/DisassembleCXXForRangeStmtVisitor.h"
#include "helpers.h"

#include "clang/Lex/Lexer.h"

namespace invisible {

// --- DisassembleCXXForRangeStmtVisitor ----------------------------

DisassembleCXXForRangeStmtVisitor::DisassembleCXXForRangeStmtVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(codePolicy(ctx)) {
    m_policy.adjustForCPlusPlus();
}

// A range-based for calls `begin()`, `end()`, `operator!=`, `operator++` and `operator*`, but none
// of these calls is in the source text, so hoist-calls and the inliner can't rewrite them. So the
// loop is written out as the iterator loop it stands for, where each call is in the text, eg.
//
//     for (auto& x : make()) use(x);   =>   {
//                                           auto &&__for_range_0 = make();
//                                           auto __for_begin_0 = __for_range_0.begin();
//                                           auto __for_end_0 = __for_range_0.end();
//                                           for (; __for_begin_0 != __for_end_0; ++__for_begin_0) {
//                                           auto& x = *__for_begin_0;
//                                           use(x);
//                                           }
//                                           }
//
// The init statement of `for (init; x : r)` comes first in the block. A range that names a
// variable, `for (auto& x : v)`, isn't bound again: the loop uses `v` itself. A temporary range,
// eg. `make()`, is bound to `auto &&`, so it lives as long as the loop, as in the original.
bool DisassembleCXXForRangeStmtVisitor::VisitCXXForRangeStmt(clang::CXXForRangeStmt* for_stmt) {
    // in a template, clang only builds the begin and end of a dependent range in the
    // instantiations, not in the template
    if (for_stmt->getBeginStmt() == nullptr) return true;
    auto& src_mgr = m_ctx.getSourceManager();
    const auto& lang_opts = m_ctx.getLangOpts();

    // clang's implicit variables, `__range1`, `__begin1` and `__end1`, get the names declared
    // below, so that the printed begin and end expressions, condition and increment use them. For
    // a range that names a variable, `for (x : v)`, the range variable is renamed to `v`, so that
    // `v.begin()` is printed.
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

    // the init statement of `for (init; x : r)` and the loop variable's declaration, as written
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

    // the header up to the body is replaced, and the two blocks closed after it
    m_rewriter.ReplaceText(clang::CharSourceRange::getCharRange(for_stmt->getBeginLoc(), body->getBeginLoc()), header);
    m_rewriter.InsertTextAfter(body_end, "\n}\n}");
    return true;
}

// Prints the begin or end expression, the condition or the increment, eg. `__for_range_0.begin()`.
// Names that aren't local are qualified, eg. `::std::begin(v)`.
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
