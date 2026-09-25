#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Sema/Sema.h"
#include <functional>
#include <string>

namespace invisible {

/// Callback invoked once the AST of an in-memory source file is ready.
using TUCallback = std::function<void(clang::ASTContext&, clang::Sema&)>;

/// Extract the (possibly rewritten) source from a Rewriter as a string.
std::string getRewrittenSource(clang::Rewriter& rewriter, const std::string& original);

/// Run a TUCallback on an in-memory source string via
/// runToolOnCodeWithArgs and return the (possibly rewritten) source.
/// Throws std::runtime_error if Clang fails to parse the source.
std::string runInMemory(const std::string& source, const std::string& filename, std::vector<std::string>& args, TUCallback callback,
                        clang::Rewriter& rewriter);

/// Run what clang -fsyntax-only does using Libtooling so we can hedge ourselves from an offending 
/// transformation and skip the edge cases. 
bool isCompilable(const std::string& source, const std::string& filename, const std::vector<std::string>& compiler_flags);

void printQualifiedVarDecl(llvm::raw_string_ostream& out, clang::VarDecl* var, const clang::PrintingPolicy& policy, clang::ASTContext& m_ctx);

bool isInsideFunction(const clang::Decl* decl);

clang::NestedNameSpecifier buildQualifierForContext(clang::ASTContext& ctx, clang::DeclContext* dc);

void qualifyNonLocalNames(clang::Stmt*& node, clang::ASTContext& ctx);

}  // namespace invisible
