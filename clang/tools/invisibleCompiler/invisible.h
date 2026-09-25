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
#include "inline/InlineVisitor.h"
#include "preprocess/information_collector/InformationCollectorVisitor.h"
#include "common/DeclBeforeDefPrinterHelper.h"
#include "preprocess/decl_before_def/DeclBeforeDefVisitor.h"
#include "preprocess/normalize_multiple_record_decl/NormalizeMultipleRecordDeclVisitor.h"
#include "preprocess/materialize_lambda/MaterializeLambdaVisitor.h"
#include "preprocess/canonicalize_var_decl/CanonicalizeVarDeclVisitor.h"
#include "preprocess/mark_local_record_decl_methods_explicit/MarkLocalRecordDeclMethodsExplicitVisitor.h"
#include "preprocess/break_local_record_decl_stmt/BreakLocalRecordDeclStmtVisitor.h"
#include "preprocess/access_specifier/AccessSpecifierVisitor.h"
#include "preprocess/name_template_type_parm/NameTemplateTypeParmVisitor.h"
#include "preprocess/short_circuit_operator_to_ir/ShortCircuitOperatorToIRVisitor.h"
#include "preprocess/mark_switch_case/MarkSwitchCaseVisitor.h"
#include "preprocess/loop_to_ir/LoopToIRVisitor.h"
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

// ─── Free functions ────────────────────────────────────────────────

/// Split comma-separated declarations into individual statements and
/// hoist nested function calls into temporaries.  Returns the
/// transformed source.
std::string preprocess(const std::string& source, const std::string& filename, std::vector<std::string>& args);

std::string oneForAll(const std::string& source, const std::string& filename, std::vector<std::string>& args);

}  // namespace invisible
