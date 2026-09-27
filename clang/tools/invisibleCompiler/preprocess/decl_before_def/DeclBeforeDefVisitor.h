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
#include "common/DeclBeforeDefPrinterHelper.h"

namespace invisible {

// ─── DeclBeforeDefVisitor ──────────────────────────────────────────────

/// Puts all function declarations before all function definitions
class DeclBeforeDefVisitor : public clang::RecursiveASTVisitor<DeclBeforeDefVisitor> {
  public:
    DeclBeforeDefVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter);
    bool VisitFunctionDecl(clang::FunctionDecl* func);
    std::string getDeferredDefinitions();
    bool shouldVisitTemplateInstantiations() const;
  private:
    clang::ASTContext& m_ctx;
    clang::Rewriter& m_rewriter;
    clang::PrintingPolicy m_policy;
    clang::PrintingPolicy m_deferred_policy_ctor;
    clang::PrintingPolicy m_deferred_policy_decl;
    clang::PrintingPolicy m_deferred_policy_decl_before_def;
    clang::PrintingPolicy m_inplace_policy;
    DeclBeforeDefPrinterHelper m_custom_printer;
    std::vector<std::string> m_deferred_definitions;
    std::vector<const clang::FunctionDecl*> m_deferred_functions;
    std::vector<std::string> m_deferred_declarations;
    /// definitions whose declaration is not emitted beforehand
    std::vector<std::string> m_undeclared_definitions;
    void fixParameterPack(std::string& str, clang::FunctionDecl* func);
    void stripConstructorTemplateArgs(std::string& str, clang::CXXConstructorDecl* ctor);
    void stripDestructorExceptionSpec(std::string& str);
    unsigned countEnclosingTemplateLevels(const clang::FunctionDecl* func_decl);
    void stripRecordDeclOnlySpecifiers(std::string& s);
    void materializeDefaultArgs(clang::Stmt* node);
    std::string printCtorInitializers(clang::CXXConstructorDecl* ctor);
    std::vector<size_t> deferredDefinitionOrder() const;
    void record_function_definition(clang::FunctionDecl* func_decl, std::string& deferred_decl, std::string& deferred_decl_before_def);
    void get_function_declaration(clang::FunctionDecl* func_decl, std::string& decl);
    void get_kr_function_declarations(clang::FunctionDecl* func_decl, std::string& decl, std::string& deferred_decl);
    clang::FriendDecl* get_friend_declaration(clang::FunctionDecl* func);
    void replace_function_declaration(clang::FunctionDecl* func, std::string& decl);
    void fix_friend_declaration(std::string& decl, const clang::FunctionTemplateDecl* tmpl,
                                const clang::PrintingPolicy& policy);
    clang::SourceLocation expand_range_to_include_template_headers(clang::SourceLocation begin);
    clang::SourceLocation expand_range_to_terminating_semi(clang::SourceLocation end);
};

}  // namespace invisible
