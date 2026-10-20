#pragma once

// The library interface of invisible: the preprocess pipeline, which turns a translation unit into
// code where every call is a statement of its own and every function a definition at the end, ready
// for the inliner (see docs/DESIGN.md section 1), and the optimizations. main.cpp is its driver.

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
#include "preprocess/materialize_lambda/MaterializeLambdaVisitor.h"
#include "preprocess/decl_before_def/KeepInPlaceVisitor.h"
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

// --- Result types --------------------------------------------------

// A stage of the preprocess pipeline.
struct PassInfo {
    std::string name;         // name used on the command line, eg. `hoist-calls`
    std::string description;  // one line summary of what the pass does
    bool repeats = false;     // whether the pass runs again until it makes no change
};

// What happened while a pass ran, printed by `--time-passes`, eg.
// `[14/14] hoist-calls              ok      37 iterations    12.40s  +11664 lines`.
struct PassStats {
    std::string name;
    unsigned iterations = 0;
    double seconds = 0;
    size_t lines_before = 0;
    size_t lines_after = 0;
};

// Hooks and limits for a preprocess run.
struct PipelineOptions {
    // Stop after the pass with this name, empty to run every pass.
    std::string stop_after;
    // Give up on a repeating pass after this many iterations, 0 for no limit.
    unsigned max_iterations = 0;
    // Before a pass, the @p index -th of @p count, runs; main.cpp prints its progress line.
    std::function<void(const PassInfo& pass, size_t index, size_t count)> on_pass_start;
    // After an iteration of a pass that compiles.
    std::function<void(const PassInfo& pass, unsigned iteration)> on_iteration;
    // After a pass, with its output; `--save-temps` writes it to `NN-<pass>.<input extension>`.
    std::function<void(const PassStats& stats, const std::string& source)> on_pass_end;
};

// Result returned by preprocess().
struct PipelineResult {
    std::string source;              // output of the last pass that succeeded
    bool succeeded = true;
    std::string failed_pass;         // pass that produced code which doesn't compile
    unsigned failed_iteration = 0;
    // The failed pass kept changing the source until PipelineOptions::max_iterations.
    // Otherwise the uncompilable source and its diagnostics are in lastCompileFailure().
    bool did_not_converge = false;
    std::vector<PassStats> passes;   // every pass that ran, including the failed one
};

// Result returned by oneForAll().
struct OptimizeResult {
    std::string source;              // output of the last pass that succeeded
    bool succeeded = true;
    std::string failed_pass;
    unsigned iterations = 0;
};

// --- Free functions ------------------------------------------------

// The passes preprocess() runs, in order, for `--list-passes` and `--stop-after`.
std::vector<PassInfo> preprocessPasses();

// Runs the preprocess passes over @p source, see docs/DESIGN.md sections 2 and 4, eg.
//
//     int total(Vec<int> &v) { return square(v.at(0)) + 1; }
//
// ends with every function declared first, every definition at the end of the TU, and every
// nested call a statement of its own:
//
//     int &__func_hoist_ret__2 = v.at(0);
//     int __func_hoist_ret__1 = square(__func_hoist_ret__2);
//     return __func_hoist_ret__1 + 1;
//
// Every iteration of every pass is checked to compile; what doesn't is recovered from, by leaving
// functions as written or rerunning from decl-before-def, and otherwise the pipeline stops there.
PipelineResult preprocess(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                          const PipelineOptions& options = {});

// Runs the information collector over @p source (the `info` mode), see
// InformationCollectorVisitor. Returns whether its output compiles, and stores that output in
// @p result.
bool collectInfo(const std::string& source, const std::string& filename, std::vector<std::string>& args, std::string& result);

// Repeats every optimization, constant folding, dead code and dead store elimination and copy
// propagation, until none of them changes the source (the `optimize` mode).
OptimizeResult oneForAll(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                         const std::function<void(unsigned iteration)>& on_iteration = {});

}  // namespace invisible
