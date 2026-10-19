# Builds in the clang tree, or standalone against an installed LLVM, eg.
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DClang_DIR=/usr/lib/llvm-22/lib/cmake/clang
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(invisible C CXX)
  find_package(Clang REQUIRED CONFIG)
  list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR} ${CLANG_CMAKE_DIR})
  include(AddLLVM)
  include(AddClang)
  include(HandleLLVMOptions)
  include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
  include_directories(${CMAKE_CURRENT_SOURCE_DIR})
  add_definitions(${LLVM_DEFINITIONS})
  add_custom_target(clang-resource-headers)
endif()

set(LLVM_LINK_COMPONENTS
  Option
  Support
  )

add_clang_tool(invisible
  invisible.cpp
  inline/InlineVisitor.cpp
  preprocess/information_collector/InformationCollectorVisitor.cpp
  common/DeclBeforeDefPrinterHelper.cpp
  preprocess/decl_before_def/DeclBeforeDefVisitor.cpp
  preprocess/materialize_lambda/MaterializeLambdaVisitor.cpp
  preprocess/decl_before_def/KeepInPlaceVisitor.cpp
  preprocess/canonicalize_var_decl/CanonicalizeVarDeclVisitor.cpp
  preprocess/functional_cast_to_c_style_cast/FunctionalCastToCStyleCastVisitor.cpp
  preprocess/mark_local_record_decl_methods_explicit/MarkLocalRecordDeclMethodsExplicitVisitor.cpp
  preprocess/break_local_record_decl_stmt/BreakLocalRecordDeclStmtVisitor.cpp
  preprocess/access_specifier/AccessSpecifierVisitor.cpp
  preprocess/name_template_type_parm/NameTemplateTypeParmVisitor.cpp
  preprocess/short_circuit_operator_to_ir/ShortCircuitOperatorToIRVisitor.cpp
  preprocess/mark_switch_case/MarkSwitchCaseVisitor.cpp
  preprocess/loop_to_ir/LoopToIRVisitor.cpp
  preprocess/disassemble_cxx_for_range_stmt/DisassembleCXXForRangeStmtVisitor.cpp
  preprocess/compound_stmt_wrapper/CompoundStmtWrapperVisitor.cpp
  preprocess/paren_expr_wrapper/ParenExprWrapperVisitor.cpp
  preprocess/comma_init/CommaInitVisitor.cpp
  preprocess/func_call_cleanup/FuncCallCleanupVisitor.cpp
  separate_functions/SeparateFunctions.cpp
  constant_fold/ConstantFoldVisitor.cpp
  dead_code_elim/DeadCodeElimVisitor.cpp
  copy_propagation/CopyPropagationVisitor.cpp
  dead_store_elim/DeadStoreElimVisitor.cpp
  helper.cpp
  main.cpp
  )

clang_target_link_libraries(invisible
  PRIVATE
  clangAST
  clangBasic
  clangFormat
  clangFrontend
  clangLex
  clangRewrite
  clangSerialization
  clangTooling
  clangToolingCore
  clangToolingRefactoring
  )
