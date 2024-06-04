set(LLVM_LINK_COMPONENTS support)

add_clang_executable(knobs
  Knobs.cpp
  )
target_link_libraries(knobs
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
  )
