#include "separate_functions/SeparateFunctions.h"
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

// --- FilterVisitor -------------------------------------------------

FilterVisitor::FilterVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter) : m_ctx(ctx), m_rewriter(rewriter) {
    
}

// a definition printed with clang's default policy for the language, eg. `int square(int x) {...}`;
// a declaration without a body isn't collected
bool FilterVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    if (!func->hasBody())
        return true;

    std::string func_str;
    llvm::raw_string_ostream stream(func_str);

    clang::PrintingPolicy policy = m_ctx.getPrintingPolicy();
    policy.Indentation = 2;

    func->print(stream, policy);
    stream.flush();

    std::string name = func->getNameInfo().getName().getAsString();
    m_functions[name] = stream.str();
    return true;
}

// the functions sorted by name, as std::map keeps them, so the output is the same for the same TU
std::string FilterVisitor::toJSON() {
    llvm::json::Object root;
    for (const auto& [name, body] : m_functions)
        root[name] = body;

    std::string result;
    llvm::raw_string_ostream stream(result);
    stream << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(root)));
    stream.flush();
    return result;
}

// --- separate_functions --------------------------------------------

std::string separate_functions(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::string result;

    clang::Rewriter rewriter;
    runInMemory(source, filename, args, [&rewriter, &result](clang::ASTContext& ctx, clang::Sema&) {
        rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        FilterVisitor visitor(ctx, rewriter);
        visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        result = visitor.toJSON();
    }, rewriter);

    return result;
}

}  // namespace invisible
