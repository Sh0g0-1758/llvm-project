#include "invisible.h"
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

// ─── preprocess ────────────────────────────────────────────────────

std::string preprocess(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::string result = source;

    // Insert braces around single-statement control flow bodies.
    {
        static const auto style = []() {
            auto s = clang::format::getLLVMStyle();
            s.InsertBraces = true;
            s.ColumnLimit = 0;
            return s;
        }();
        auto replacements = clang::format::reformat(
            style, result,
            {clang::tooling::Range(0, result.size())});
        auto formatted =
            clang::tooling::applyAllReplacements(result, replacements);
        if (formatted)
            result = std::move(*formatted);
    }

    std::string pass_to_run = args[args.size() - 1];
    args.pop_back();

    // Suppress Clang's implicit builtin declarations so that hoisted
    // temporaries use the type names from the user's headers (e.g.
    // `size_t`) rather than Clang-internal typedefs (`__size_t`).
    args.emplace_back("-fno-builtin");
    // Suppress warnings that show up as errors while parsing using
    // libtooling but are otherwise benign as still compiles the TU. 
    // For eg. -Wincompatible-pointer-types
    args.emplace_back("-Wno-everything");

    // collects information about stuff for you. 
    if (pass_to_run == "info") {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            if (ctx.getLangOpts().CPlusPlus) {
                InformationCollectorVisitor access_visitor(ctx, rewriter);
                access_visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Info Collector failed\n";
            return tmp_result;
        }
        return result;
    }

    // marks every access specifier as public and unconditionally
    // adds a public access specifier at the start of every record
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            if (ctx.getLangOpts().CPlusPlus) {
                AccessSpecifierVisitor access_visitor(ctx, rewriter);
                access_visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Access Specifier visitor failed\n";
            return tmp_result;
        }
    }

    llvm::outs() << "[Invisible]: Access Specifier visitor succeeded\n";

    // materializes a name for unnamed template types
    // eg. converts template<typename = void> to template<typename `id` = void>
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            if (ctx.getLangOpts().CPlusPlus) {
                NameTemplateTypeParmVisitor access_visitor(ctx, rewriter);
                access_visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Name Template Type Param visitor failed\n";
            return tmp_result;
        }
    }

    llvm::outs() << "[Invisible]: Name Template Type Param Visitor succeeded\n";

    {
        bool changed = false;
        do {
            clang::Rewriter rewriter;
            std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &changed](clang::ASTContext& ctx, clang::Sema& sema) {
                rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
                MaterializeLambdaVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                changed = visitor.changed();
            }, rewriter);
            if(isCompilable(tmp_result, filename, args)) {
                result = std::move(tmp_result);
            } else {
                llvm::outs() << "MaterializeLambdaVisitor failed\n";
                return tmp_result;
            }
        } while (changed);
    }

    llvm::outs() << "[Invisible]: MaterializeLambdaVisitor succeeded\n";

    // Gather function definitions and move them after all function
    // declarations and Canonicalize all local / global variables defined
    // in a translation unit.
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            // The trick here is to make the changes within the AST
            {
                FunctionalCastToCStyleCastVisitor visitor(ctx, sema);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }

            {
                CanonicalizeVarDeclVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }

            // clang printer by default hides all LocalrecordDecls in template instantiations
            // whose DeclStmt has multiple decls. 
            {
                BreakLocalRecordDeclStmtVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }

            // clang printer by default does not print implicit function instantiations
            {
                MarkLocalRecordDeclMethodsExplicitVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }

            // declarations are kept as written, respell the references to
            // the global variables renamed above.
            {
                RenameGlobalVarRefsVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }

            {
                DeclBeforeDefVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());

                auto& SM = ctx.getSourceManager();
                auto endOfFile = SM.getLocForEndOfFile(SM.getMainFileID());
                rewriter.InsertTextAfter(endOfFile, visitor.getDeferredDefinitions());
            }

        }, rewriter);
        // Remove redefinitions of any record that might have been
        // introduced because of separating Multiple Declarators
        // onto separate statements.
        {
            clang::Rewriter rewriter;
            tmp_result = runInMemory(tmp_result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
                rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
                NormalizeMultipleRecordDeclVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }, rewriter);
        }
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Declarations Before Definitions Preprocessing failed\n";
            return tmp_result;
        }
    }

    llvm::outs() << "[Invisible]: DeclBeforeDef visitor succeeded\n";

    // Marks the beginning of every case statement with a semi-colon so
    // that any function call present at the start of the set of statements
    // for case is hoisted as part of the case and not above it.
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            MarkSwitchCaseVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Switch-case Preprocessing failed\n";
            return tmp_result;
        }
    }

    llvm::outs() << "[Invisible]: MarkSwitchCase visitor succeeded\n";

    // Wraps statements which may have a declStmt (for/if/switch) with a compound stmt
    // This helps provide a valid space to hoist a comma separated declarator list
    // and also valid space for any loop rewrite. 
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            CompoundStmtWrapperVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "CompoundStmtWrapperVisitor failed\n";
            return tmp_result;
        }
    }

    llvm::outs() << "[Invisible]: CompoundStmtWrapper visitor succeeded\n";

    // Wraps binary operator (,) with a paranthesis. Because the binary operator
    // can occur without any wrapper as well which can lead to errors while inlining
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            ParenExprWrapperVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "ParenExprWrapperVisitor failed\n";
            return tmp_result;
        }
    }

    llvm::outs() << "[Invisible]: ParenExprWrapperVisitor succeeded\n";

    // Writes Loops in their IR form at the source code level. 
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            LoopToIRVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "Loops Preprocessing failed\n";
            return tmp_result;
        }
    }

    llvm::outs() << "[Invisible]: LoopToIRVisitor succeeded\n";

    // Splits comma-separated variable declarations into individual statements
    {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            CommaInitVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }, rewriter);
        // Remove redefinitions of any record that might have been
        // introduced because of separating Multiple Declarators
        // onto separate statements.
        {
            clang::Rewriter rewriter;
            tmp_result = runInMemory(tmp_result, filename, args, [&rewriter](clang::ASTContext& ctx, clang::Sema& sema) {
                rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
                NormalizeMultipleRecordDeclVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            }, rewriter);
        }
        if(isCompilable(tmp_result, filename, args)) {
            result = std::move(tmp_result);
        } else {
            llvm::outs() << "CommaInitVisitor failed\n";
            return tmp_result;
        }
    }

    llvm::outs() << "[Invisible]: CommaInitVisitor succeeded\n";

    size_t current_index;

    /// rewrite short-circuit operations into their IR form at the source code level
    bool didConvertToIR = true;
    current_index = 0;
    while (didConvertToIR) {
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &current_index, &didConvertToIR](clang::ASTContext& ctx, clang::Sema& sema) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            ShortCircuitOperatorToIRVisitor visitor(ctx, rewriter, current_index);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            didConvertToIR = visitor.didConvertToIR();
        }, rewriter);
        if(!isCompilable(tmp_result, filename, args)) {
            llvm::outs() << "Short-circuit Preprocessing failed\n";
            return tmp_result;
        }
        result = std::move(tmp_result);
    }

    llvm::outs() << "[Invisible]: ShortCircuitOperatorToIRVisitor succeeded\n";

    llvm::outs() << "started folding\n";
    // constant fold mathematical expressions into their simplified form
    bool did_fold = true;
    while (did_fold) {
        did_fold = false;
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &did_fold](clang::ASTContext& ctx, clang::Sema&) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            constantFoldVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            did_fold = visitor.did_fold();
        }, rewriter);
        if(!isCompilable(tmp_result, filename, args)) {
            llvm::outs() << "Constant Folding Preprocessing failed\n";
            return tmp_result;
        }
        result = std::move(tmp_result);
    }

    llvm::outs() << "[Invisible]: constantFoldVisitor succeeded\n";

    llvm::outs() << "started hoisting\n";
    // Hoist nested function calls (repeat until stable).
    bool did_cleanup = true;
    current_index = 0;
    while (did_cleanup) {
        did_cleanup = false;
        clang::Rewriter rewriter;
        std::string tmp_result = runInMemory(result, filename, args, [&rewriter, &did_cleanup, &current_index](clang::ASTContext& ctx, clang::Sema&) {
            rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
            FuncCallCleanupVisitor visitor(ctx, rewriter, current_index);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            did_cleanup = visitor.did_cleanup();
        }, rewriter);
        if(!isCompilable(tmp_result, filename, args)) {
            llvm::outs() << "Function Hoisting Preprocessing failed\n";
            return tmp_result;
        }
        result = std::move(tmp_result);
    }

    llvm::outs() << "[Invisible]: FuncCallCleanupVisitor succeeded\n";

    return result;
}

// ---- one-for-all ------------------------------------------------------------
std::string oneForAll(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::string result = source;

    // Suppress Clang's implicit builtin declarations so that hoisted
    // temporaries use the type names from the user's headers (e.g.
    // `size_t`) rather than Clang-internal typedefs (`__size_t`).
    args.emplace_back("-fno-builtin");
    // Suppress warnings that show up as errors while parsing using
    // libtooling but are otherwise benign as still compiles the TU. 
    // For eg. -Wincompatible-pointer-types
    args.emplace_back("-Wno-everything");

    bool did_change = true;
    int num_passes = 0;
    while (did_change) {
        did_change = false;
        bool is_compilable = true;
        result = constant_fold(result, filename, args, &did_change, &is_compilable);
        if(!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY CONSTANT-FOLD\n";
            return result;
        }
        result = deadCodeElim(result, filename, args, &did_change, &is_compilable);
        if(!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY DEAD-CODE-ELIMINATION\n";
            return result;
        }
        result = copyPropagation(result, filename, args, &did_change, &is_compilable);
        if(!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY COPY-PROPAGATION\n";
            return result;
        }
        result = constant_fold(result, filename, args, &did_change, &is_compilable);
        if(!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY CONSTANT-FOLD\n";
            return result;
        }
        result = deadCodeElim(result, filename, args, &did_change, &is_compilable);
        if (!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY DEAD-CODE-ELIMINATION\n";
            return result;
        }
        result = deadStoreElim(result, filename, args, &did_change, &is_compilable);
        if(!is_compilable) {
            llvm::outs() << "[OPTIMIZE]: INVALID TRANSFORMATION BY DEAD-STORE-ELIMINATION\n";
            return result;
        }
        llvm::outs() << "[OPTIMIZE]: Done with iteration> " << ++num_passes << "\n";
    }

    llvm::outs() << "[OPTIMIZE]: CONVERGED AT ITERATION> " << num_passes << "\n";
    return result;
}

}  // namespace invisible

/*
export UV_CONCURRENT_DOWNLOADS=4
export UV_CONCURRENT_INSTALLS=4
export UV_BUILD_JOBS=2
export UV_INDEX_PRIVATE_REGISTRY_USERNAME=aws
export UV_INDEX_PRIVATE_REGISTRY_PASSWORD=$(aws codeartifact get-authorization-token --domain reveng --domain-owner 127214196211 --query authorizationToken --output text --profile compiler-engineer-codeartifact --region eu-west-2)
ulimit -v $((8 * 1024 * 1024))
taskset -c 0-7 uv pip install -e . --no-cache --reinstall
*/
