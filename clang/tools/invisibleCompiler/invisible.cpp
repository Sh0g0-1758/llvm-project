#include "invisible.h"
#include "helpers.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "clang/AST/TemplateBase.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <queue>
#include <stack>

namespace invisible {

// ─── preprocess ────────────────────────────────────────────────────

namespace {

using Visit = std::function<void(clang::ASTContext&, clang::Sema&, clang::Rewriter&)>;

// clang prints `__typeof(x)` as `typeof(x)`, which is a keyword in C23 and the GNU
// modes only. `__typeof__` is one in every mode.
std::string spellTypeofAsKeyword(const std::string& source) {
    clang::LangOptions lang_opts;
    lang_opts.CPlusPlus = true;
    clang::Lexer lexer(clang::SourceLocation(), lang_opts, source.data(), source.data(), source.data() + source.size());
    std::string result;
    size_t copied = 0;
    clang::Token token;
    do {
        lexer.LexFromRawLexer(token);
        if (!token.is(clang::tok::raw_identifier)) continue;
        llvm::StringRef name = token.getRawIdentifier();
        if (name != "typeof" && name != "typeof_unqual") continue;
        size_t at = name.data() - source.data();
        result.append(source, copied, at - copied).append("__" + name.str() + "__");
        copied = at + name.size();
    } while (token.isNot(clang::tok::eof));
    return result.append(source, copied, std::string::npos);
}

/// Runs @p visit on the AST of @p source and returns the rewritten source.
std::string rewrite(const std::string& source, const std::string& filename, std::vector<std::string>& args, const Visit& visit) {
    clang::Rewriter rewriter;
    return spellTypeofAsKeyword(runInMemory(source, filename, args, [&rewriter, &visit](clang::ASTContext& ctx, clang::Sema& sema) {
        rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        visit(ctx, sema, rewriter);
    }, rewriter));
}

// Insert braces around single-statement control flow bodies.
std::string insertBraces(const std::string& source) {
    static const auto style = []() {
        auto s = clang::format::getLLVMStyle();
        s.InsertBraces = true;
        s.ColumnLimit = 0;
        return s;
    }();
    auto replacements = clang::format::reformat(style, source, {clang::tooling::Range(0, source.size())});
    auto formatted = clang::tooling::applyAllReplacements(source, replacements);
    return formatted ? std::move(*formatted) : source;
}

struct Pass {
    PassInfo info;
    /// Runs one iteration on @p source and returns its output. A repeating pass
    /// sets @p changed to run again.
    std::function<std::string(const std::string& source, unsigned iteration, bool& changed)> run;
};

std::vector<Pass> buildPreprocessPasses(const std::string& filename, std::vector<std::string>& args, size_t& current_index) {
    std::vector<Pass> passes;

    passes.push_back({{"insert-braces", "Add braces around single statement control flow bodies", false},
        [](const std::string& source, unsigned, bool&) { return insertBraces(source); }});

    // marks every access specifier as public and unconditionally
    // adds a public access specifier at the start of every record
    passes.push_back({{"access-specifier", "Make every class member public", false},
        [&](const std::string& source, unsigned, bool&) {
            return rewrite(source, filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                if (ctx.getLangOpts().CPlusPlus) {
                    AccessSpecifierVisitor access_visitor(ctx, rewriter);
                    access_visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                }
            });
        }});

    // materializes a name for unnamed template types
    // eg. converts template<typename = void> to template<typename `id` = void>
    passes.push_back({{"name-template-type-parm", "Name unnamed template type parameters", false},
        [&](const std::string& source, unsigned, bool&) {
            return rewrite(source, filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                if (ctx.getLangOpts().CPlusPlus) {
                    NameTemplateTypeParmVisitor access_visitor(ctx, rewriter);
                    access_visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                }
            });
        }});

    passes.push_back({{"materialize-lambda", "Turn lambdas into named structs", true},
        [&](const std::string& source, unsigned, bool& changed) {
            return rewrite(source, filename, args, [&changed](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                MaterializeLambdaVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                changed = visitor.changed();
            });
        }});

    // Gather function definitions and move them after all function
    // declarations and Canonicalize all local variables and parameters defined
    // in a translation unit.
    passes.push_back({{"decl-before-def", "Declare every function before any definition, specializing template instantiations", false},
        [&](const std::string& source, unsigned, bool&) {
            return rewrite(source, filename, args, [](clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter) {
                // before the AST is changed below
                std::set<const clang::FunctionDecl*> kept_in_place = KeepInPlaceVisitor(ctx).run();

                // the constructors moving and copying hoisted temporaries will call
                instantiateConstructorsOfValues(ctx, sema);

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

                {
                    DeclBeforeDefVisitor visitor(ctx, rewriter, kept_in_place);
                    visitor.TraverseDecl(ctx.getTranslationUnitDecl());

                    auto& SM = ctx.getSourceManager();
                    auto endOfFile = SM.getLocForEndOfFile(SM.getMainFileID());
                    rewriter.InsertTextAfter(endOfFile, visitor.getDeferredDefinitions());
                }
            });
        }});

    // Marks the beginning of every case statement with a semi-colon so
    // that any function call present at the start of the set of statements
    // for case is hoisted as part of the case and not above it.
    passes.push_back({{"mark-switch-case", "Start the statements of every case label with an empty statement", false},
        [&](const std::string& source, unsigned, bool&) {
            return rewrite(source, filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                MarkSwitchCaseVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            });
        }});

    // Wraps statements which may have a declStmt (for/if/switch) with a compound stmt
    // This helps provide a valid space to hoist a comma separated declarator list
    // and also valid space for any loop rewrite. 
    passes.push_back({{"compound-stmt-wrapper", "Wrap for, if and switch statements in a block", false},
        [&](const std::string& source, unsigned, bool&) {
            return rewrite(source, filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                CompoundStmtWrapperVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            });
        }});

    // Wraps binary operator (,) with a paranthesis. Because the binary operator
    // can occur without any wrapper as well which can lead to errors while inlining
    passes.push_back({{"paren-expr-wrapper", "Parenthesize comma operator expressions", false},
        [&](const std::string& source, unsigned, bool&) {
            return rewrite(source, filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                ParenExprWrapperVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            });
        }});

    passes.push_back({{"disassemble-cxx-for-range-stmt", "Rewrite range based for loops as for loops", false},
        [&](const std::string& source, unsigned, bool&) {
            return rewrite(source, filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                if (ctx.getLangOpts().CPlusPlus) {
                    DisassembleCXXForRangeStmtVisitor visitor(ctx, rewriter);
                    visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                }
            });
        }});

    // Writes Loops in their IR form at the source code level. 
    passes.push_back({{"loop-to-ir", "Rewrite loops into their IR form", false},
        [&](const std::string& source, unsigned, bool&) {
            return rewrite(source, filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                LoopToIRVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            });
        }});

    // Splits comma-separated variable declarations into individual statements
    passes.push_back({{"comma-init", "Split comma separated declarations into one declaration each", false},
        [&](const std::string& source, unsigned, bool&) {
            return rewrite(source, filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                CommaInitVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
            });
        }});

    /// rewrite short-circuit operations into their IR form at the source code level
    passes.push_back({{"short-circuit-to-ir", "Rewrite && and || into their IR form", true},
        [&](const std::string& source, unsigned iteration, bool& changed) {
            if (iteration == 0) current_index = 0;
            return rewrite(source, filename, args, [&](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                ShortCircuitOperatorToIRVisitor visitor(ctx, rewriter, current_index);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                changed = visitor.didConvertToIR();
            });
        }});

    // constant fold mathematical expressions into their simplified form
    passes.push_back({{"constant-fold", "Fold constant expressions", true},
        [&](const std::string& source, unsigned, bool& changed) {
            return rewrite(source, filename, args, [&changed](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                constantFoldVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                changed = visitor.did_fold();
            });
        }});

    // Hoist nested function calls (repeat until stable).
    passes.push_back({{"hoist-calls", "Hoist nested function calls into temporaries", true},
        [&](const std::string& source, unsigned iteration, bool& changed) {
            if (iteration == 0) current_index = 0;
            return rewrite(source, filename, args, [&](clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter) {
                FuncCallCleanupVisitor visitor(ctx, sema, rewriter, current_index);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                changed = visitor.did_cleanup();
            });
        }});

    return passes;
}

size_t countLines(const std::string& text) {
    return std::count(text.begin(), text.end(), '\n');
}

}  // namespace

std::vector<PassInfo> preprocessPasses() {
    std::vector<std::string> args;
    size_t current_index = 0;
    std::vector<PassInfo> infos;
    for (const Pass& pass : buildPreprocessPasses("", args, current_index))
        infos.push_back(pass.info);
    return infos;
}

PipelineResult preprocess(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                          const PipelineOptions& options) {
    addPipelineFlags(args);

    size_t current_index = 0;
    std::vector<Pass> passes = buildPreprocessPasses(filename, args, current_index);

    PipelineResult out;
    out.source = source;
    for (size_t i = 0; i < passes.size(); i++) {
        const Pass& pass = passes[i];
        if (options.on_pass_start) options.on_pass_start(pass.info, i, passes.size());

        PassStats stats;
        stats.name = pass.info.name;
        stats.lines_before = countLines(out.source);
        auto start = std::chrono::steady_clock::now();
        auto finish = [&]() {
            stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            stats.lines_after = countLines(out.source);
            out.passes.push_back(stats);
        };

        bool changed;
        do {
            changed = false;
            llvm::PrettyStackTraceFormat crash_note("running pass '%s', iteration %u", pass.info.name.c_str(), stats.iterations + 1);
            std::string next = pass.run(out.source, stats.iterations, changed);
            stats.iterations++;
            if (!isCompilable(next, filename, args)) {
                out.succeeded = false;
                out.failed_pass = pass.info.name;
                out.failed_iteration = stats.iterations;
                finish();
                return out;
            }
            out.source = std::move(next);
            if (options.on_iteration) options.on_iteration(pass.info, stats.iterations);
            if (changed && options.max_iterations && stats.iterations >= options.max_iterations) {
                out.succeeded = false;
                out.did_not_converge = true;
                out.failed_pass = pass.info.name;
                out.failed_iteration = stats.iterations;
                finish();
                return out;
            }
        } while (pass.info.repeats && changed);

        finish();
        if (options.on_pass_end) options.on_pass_end(out.passes.back(), out.source);
        if (pass.info.name == options.stop_after) break;
    }
    return out;
}

// collects information about stuff for you. 
bool collectInfo(const std::string& source, const std::string& filename, std::vector<std::string>& args, std::string& result) {
    addPipelineFlags(args);
    result = rewrite(insertBraces(source), filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
        if (ctx.getLangOpts().CPlusPlus) {
            InformationCollectorVisitor access_visitor(ctx, rewriter);
            access_visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }
    });
    return isCompilable(result, filename, args);
}

// ---- one-for-all ------------------------------------------------------------
OptimizeResult oneForAll(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                         const std::function<void(unsigned iteration)>& on_iteration) {
    addPipelineFlags(args);

    using Optimization = std::string (*)(const std::string&, const std::string&, std::vector<std::string>&, bool*, bool*);
    static const std::pair<const char*, Optimization> order[] = {
        {"constant-fold", constant_fold}, {"dce", deadCodeElim}, {"copy-propagate", copyPropagation},
        {"constant-fold", constant_fold}, {"dce", deadCodeElim}, {"dead-store-elim", deadStoreElim},
    };

    OptimizeResult out;
    out.source = source;
    bool did_change = true;
    while (did_change) {
        did_change = false;
        for (const auto& [name, optimization] : order) {
            bool is_compilable = true;
            llvm::PrettyStackTraceFormat crash_note("running optimization '%s', iteration %u", name, out.iterations + 1);
            std::string result = optimization(out.source, filename, args, &did_change, &is_compilable);
            if (!is_compilable) {
                out.succeeded = false;
                out.failed_pass = name;
                return out;
            }
            out.source = std::move(result);
        }
        out.iterations++;
        if (on_iteration) on_iteration(out.iterations);
    }
    return out;
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
