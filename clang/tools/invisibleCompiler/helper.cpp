#include "helpers.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

#include <stdexcept>

namespace invisible {

// ─── Clang in-memory pass infrastructure ───────────────────────────

std::string getRewrittenSource(clang::Rewriter& rewriter, const std::string& original) {
    auto& SM = rewriter.getSourceMgr();
    const llvm::RewriteBuffer* buf = rewriter.getRewriteBufferFor(SM.getMainFileID());
    if (buf)
        return std::string(buf->begin(), buf->end());
    return original;
}

namespace {

class InvisibleConsumer : public clang::ASTConsumer {
  public:
    explicit InvisibleConsumer(TUCallback callback, clang::CompilerInstance& ci) : m_callback(std::move(callback)), m_ci(ci) {}

    void HandleTranslationUnit(clang::ASTContext& ctx) override { m_callback(ctx, m_ci.getSema()); }

  private:
    TUCallback m_callback;
    clang::CompilerInstance& m_ci;
};

class InvisibleAction : public clang::ASTFrontendAction {
  public:
    explicit InvisibleAction(TUCallback callback) : m_callback(std::move(callback)) {}

  protected:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& ci, llvm::StringRef) override {
        return std::make_unique<InvisibleConsumer>(m_callback, ci);
    }

  private:
    TUCallback m_callback;
};

}  // namespace

std::string runInMemory(const std::string& source, const std::string& filename, std::vector<std::string>& args, TUCallback callback,
                        clang::Rewriter& rewriter) {
    auto action = std::make_unique<InvisibleAction>(std::move(callback));
    if (!clang::tooling::runToolOnCodeWithArgs(std::move(action), source, args, filename))
        llvm::outs() << "Clang failed to process: " << filename;
    return getRewrittenSource(rewriter, source);
}

bool isCompilable(const std::string& source, const std::string& filename,
                  const std::vector<std::string>& compiler_flags) {
    class CheckAction : public clang::SyntaxOnlyAction {
    public:
        bool hasErrors = false;
        void EndSourceFile() override {
            hasErrors = getCompilerInstance().getDiagnostics().hasErrorOccurred();
            clang::SyntaxOnlyAction::EndSourceFile();
        }
    };

    auto action = std::make_unique<CheckAction>();
    auto* raw = action.get();
    clang::tooling::runToolOnCodeWithArgs(std::move(action), source, compiler_flags, filename);
    return !raw->hasErrors;
}

void printQualifiedVarDecl(llvm::raw_string_ostream& out, clang::VarDecl* var, const clang::PrintingPolicy& policy, clang::ASTContext& m_ctx) {
    std::string name;
    {
        llvm::raw_string_ostream name_out(name);
        var->printNestedNameSpecifier(name_out, policy);
        name_out << var->getName();
        name_out.flush();
    }

    struct ScopedDeclName {
        clang::NamedDecl* m_decl;
        clang::DeclarationName saved;
        ScopedDeclName(clang::NamedDecl* decl, clang::DeclarationName new_decl)
            : m_decl(decl), saved(m_decl->getDeclName()) { m_decl->setDeclName(new_decl); }
        ~ScopedDeclName() { m_decl->setDeclName(saved); }
    };

    ScopedDeclName guard(var, clang::DeclarationName(&m_ctx.Idents.get(name)));
    var->print(out, policy);
}

}  // namespace invisible
