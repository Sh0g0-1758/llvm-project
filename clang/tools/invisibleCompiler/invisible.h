#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Sema/Sema.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/FoldingSet.h"
#include "clang/AST/ASTTypeTraits.h"

#include <map>
#include <unordered_map>
#include <deque>
#include <string>
#include <vector>
#include <cassert>
#include <functional>
#include "inline/InlineVisitor.h"
#include "preprocess/information_collector/InformationCollectorVisitor.h"
#include "common/DeclBeforeDefPrinterHelper.h"
#include "preprocess/decl_before_def/DeclBeforeDefVisitor.h"
#include "preprocess/normalize_multiple_record_decl/NormalizeMultipleRecordDeclVisitor.h"
#include "preprocess/materialize_lambda/MaterializeLambdaVisitor.h"
#include "preprocess/clone_constant_evaluated/CloneConstantEvaluatedVisitor.h"
#include "preprocess/canonicalize_var_decl/CanonicalizeVarDeclVisitor.h"
#include "preprocess/functional_cast_to_c_style_cast/FunctionalCastToCStyleCastVisitor.h"
#include "preprocess/mark_local_record_decl_methods_explicit/MarkLocalRecordDeclMethodsExplicitVisitor.h"
#include "preprocess/break_local_record_decl_stmt/BreakLocalRecordDeclStmtVisitor.h"
#include "preprocess/access_specifier/AccessSpecifierVisitor.h"
#include "preprocess/name_template_type_parm/NameTemplateTypeParmVisitor.h"
#include "preprocess/short_circuit_operator_to_ir/ShortCircuitOperatorToIRVisitor.h"
#include "preprocess/mark_switch_case/MarkSwitchCaseVisitor.h"
#include "preprocess/loop_to_ir/LoopToIRVisitor.h"
#include "preprocess/disassemble_cxx_for_range_stmt/DisassembleCXXForRangeStmtVisitor.h"
#include "preprocess/compound_stmt_wrapper/CompoundStmtWrapperVisitor.h"
#include "preprocess/paren_expr_wrapper/ParenExprWrapperVisitor.h"
#include "preprocess/comma_init/CommaInitVisitor.h"
#include "preprocess/func_call_cleanup/FuncCallCleanupVisitor.h"
#include "separate_functions/SeparateFunctions.h"
#include "constant_fold/ConstantFoldVisitor.h"
#include "dead_code_elim/DeadCodeElimVisitor.h"
#include "copy_propagation/CopyPropagationVisitor.h"
#include "dead_store_elim/DeadStoreElimVisitor.h"

namespace invisible {

// ─── Result types ──────────────────────────────────────────────────

/// A stage of the preprocess pipeline.
struct PassInfo {
    std::string name;         ///< name used on the command line, eg. `hoist-calls`
    std::string description;  ///< one line summary of what the pass does
    bool repeats = false;     ///< whether the pass runs again until it makes no change
};

/// What happened while a pass ran.
struct PassStats {
    std::string name;
    unsigned iterations = 0;
    double seconds = 0;
    size_t lines_before = 0;
    size_t lines_after = 0;
};

/// Hooks and limits for a preprocess run.
struct PipelineOptions {
    /// Stop after the pass with this name, empty to run every pass.
    std::string stop_after;
    /// Give up on a repeating pass after this many iterations, 0 for no limit.
    unsigned max_iterations = 0;
    std::function<void(const PassInfo& pass, size_t index, size_t count)> on_pass_start;
    std::function<void(const PassInfo& pass, unsigned iteration)> on_iteration;
    std::function<void(const PassStats& stats, const std::string& source)> on_pass_end;
};

/// Result returned by preprocess().
struct PipelineResult {
    std::string source;              ///< output of the last pass that succeeded
    bool succeeded = true;
    std::string failed_pass;         ///< pass that produced code which doesn't compile
    unsigned failed_iteration = 0;
    /// The failed pass kept changing the source until PipelineOptions::max_iterations.
    /// Otherwise the uncompilable source and its diagnostics are in lastCompileFailure().
    bool did_not_converge = false;
    std::vector<PassStats> passes;   ///< every pass that ran, including the failed one
};

/// Result returned by oneForAll().
struct OptimizeResult {
    std::string source;              ///< output of the last pass that succeeded
    bool succeeded = true;
    std::string failed_pass;
    unsigned iterations = 0;
};

// ─── Free functions ────────────────────────────────────────────────

/// The passes preprocess() runs, in order.
std::vector<PassInfo> preprocessPasses();

/// Split comma-separated declarations into individual statements and
/// hoist nested function calls into temporaries. Every pass is checked to
/// produce compilable code, and the pipeline stops at the first that doesn't.
PipelineResult preprocess(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                          const PipelineOptions& options = {});

/// Run the information collector over @p source. Returns whether its output
/// compiles, and stores that output in @p result.
bool collectInfo(const std::string& source, const std::string& filename, std::vector<std::string>& args, std::string& result);

/// Repeat every optimization until none of them changes the source.
OptimizeResult oneForAll(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                         const std::function<void(unsigned iteration)>& on_iteration = {});

}  // namespace invisible
