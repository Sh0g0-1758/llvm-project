#pragma once

#include "clang/AST/ASTContext.h"
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

}  // namespace invisible
