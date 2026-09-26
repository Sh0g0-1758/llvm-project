#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Sema/Sema.h"
#include <functional>
#include <string>

namespace invisible {

/// Settings shared by every pass, set once by the command line driver.
struct ToolSettings {
    bool verbose = false;  ///< show the passes' progress chatter
    bool color = false;    ///< color clang's diagnostics
};
ToolSettings& settings();

/// Stream for the passes' progress chatter, only shown with --verbose.
llvm::raw_ostream& verbose();

/// Add the flags every pipeline parses with to the user's clang flags.
void addPipelineFlags(std::vector<std::string>& args);

/// The last source isCompilable() rejected, and what clang reported for it.
struct CompileFailure {
    std::string source;
    std::string filename;     ///< name the diagnostics refer to
    std::string diagnostics;  ///< clang's rendered diagnostics
    unsigned errors = 0;
};
const CompileFailure& lastCompileFailure();

/// Callback invoked once the AST of an in-memory source file is ready.
using TUCallback = std::function<void(clang::ASTContext&, clang::Sema&)>;

/// Extract the (possibly rewritten) source from a Rewriter as a string.
std::string getRewrittenSource(clang::Rewriter& rewriter, const std::string& original);

/// Run a TUCallback on an in-memory source string via
/// runToolOnCodeWithArgs and return the (possibly rewritten) source.
/// Clang's diagnostics go to verbose(), since the caller checks the result with isCompilable().
std::string runInMemory(const std::string& source, const std::string& filename, std::vector<std::string>& args, TUCallback callback,
                        clang::Rewriter& rewriter);

/// Run what clang -fsyntax-only does using Libtooling so we can hedge ourselves from an offending 
/// transformation and skip the edge cases. Diagnostics aren't printed; when the source
/// doesn't compile, it is recorded with them in lastCompileFailure().
bool isCompilable(const std::string& source, const std::string& filename, const std::vector<std::string>& compiler_flags);

void printQualifiedVarDecl(llvm::raw_string_ostream& out, clang::VarDecl* var, const clang::PrintingPolicy& policy, clang::ASTContext& m_ctx);

bool isInsideFunction(const clang::Decl* decl);

/**
 * Declarations clang creates while instantiating a class template point to the
 * source text of the pattern they were copied from. Besides template instantiations
 * this includes copies that are not marked as one, eg. members of the instantiated
 * class that are explicitly specialized later on, member templates and friends.
 * Such declarations must not rewrite the pattern's text.
 */
bool isInstantiatedCopy(const clang::FunctionDecl* func);

/// On targets where `__builtin_va_list` is an array (eg. x86-64 `__va_list_tag[1]`),
/// a va_list parameter is printed as `__va_list_tag *` (decayed) or, when printed
/// canonically, as `__va_list_tag name[1]`.
/// `__va_list_tag` is a compiler internal name that can't be spelled in source, so
/// respell it as `__builtin_va_list`, which decays back to the same parameter type.
void respellDecayedVaList(std::string& text);

clang::NestedNameSpecifier buildQualifierForContext(clang::ASTContext& ctx, clang::DeclContext* dc);

void qualifyNonLocalNames(clang::Stmt*& node, clang::ASTContext& ctx);

}  // namespace invisible
