set(LLVM_LINK_COMPONENTS
  Option
  Support
  )

add_clang_tool(invisible
  Invisible.cpp
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
