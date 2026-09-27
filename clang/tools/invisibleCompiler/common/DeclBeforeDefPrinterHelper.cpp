#include "common/DeclBeforeDefPrinterHelper.h"
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

// ─── DeclBeforeDefPrinterHelper ──────────────────────────────────────────────

DeclBeforeDefPrinterHelper::DeclBeforeDefPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy)
    : m_ctx(ctx),
      m_policy(policy) {
        if (m_ctx.getLangOpts().CPlusPlus)
            m_policy.adjustForCPlusPlus();
        m_policy.IncludeTagDefinition = true;
      }

bool DeclBeforeDefPrinterHelper::handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) {
    if (auto* gcc_asm = llvm::dyn_cast<clang::GCCAsmStmt>(stmt)) {
        std::string asm_stmt;
        llvm::raw_string_ostream asm_out(asm_stmt);
        gcc_asm->printPretty(asm_out, nullptr, m_policy);
        asm_out.flush();

        asm_stmt.replace(0, 3, "__asm__");
        stream << asm_stmt << ";";
        return true;
    }

    // `sizeof...(Ts)` in an instantiation is printed as its value, a specialization
    // doesn't have the pack.
    if (auto* pack_size = llvm::dyn_cast<clang::SizeOfPackExpr>(stmt); pack_size && !pack_size->isValueDependent()) {
        llvm::APSInt length(llvm::APInt(m_ctx.getTypeSize(pack_size->getType()), pack_size->getPackLength()), true);
        stream << length << integerSuffix(m_ctx, length, pack_size->getType());
        return true;
    }

    return false;
}

}  // namespace invisible
