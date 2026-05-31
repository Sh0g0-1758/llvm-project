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

}  // namespace invisible
