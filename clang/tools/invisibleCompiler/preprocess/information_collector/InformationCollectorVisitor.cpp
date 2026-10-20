#include "preprocess/information_collector/InformationCollectorVisitor.h"
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

// --- InformationCollectorVisitor ----------------------------------------------

InformationCollectorVisitor::InformationCollectorVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter) { }

bool InformationCollectorVisitor::shouldVisitTemplateInstantiations() const { return true; }

// eg. for `template <class T> T id(T)` called with an int, the stats of `id` and of `id<int>`,
// which tell why decl-before-def treats a function as it does
bool InformationCollectorVisitor::VisitFunctionDecl(clang::FunctionDecl* func) {
    std::string function_name = func->getNameAsString();
    llvm::outs() << "=====\nStats for function : [" << function_name << "]\n";

    llvm::outs() << "hasBody                          : " << func->hasBody() << "\n";
    llvm::outs() << "isThisDeclarationADefinition     : " << func->isThisDeclarationADefinition() << "\n";
    llvm::outs() << "doesThisDeclarationHaveABody     : " << func->doesThisDeclarationHaveABody() << "\n";
    llvm::outs() << "isDefined                        : " << func->isDefined() << "\n";
    llvm::outs() << "isFirstDecl                      : " << func->isFirstDecl() << "\n";

    llvm::outs() << "isDependentContext               : " << func->isDependentContext() << "\n";
    llvm::outs() << "isTemplateInstantiation          : " << func->isTemplateInstantiation() << "\n";
    llvm::outs() << "isFunctionTemplateSpecialization : " << func->isFunctionTemplateSpecialization() << "\n";
    llvm::outs() << "isOutOfLine                      : " << func->isOutOfLine() << "\n";

    llvm::outs() << "getNumTemplateParameterLists     : " << func->getNumTemplateParameterLists() << "\n";
    llvm::outs() << "getDescribedFunctionTemplate     : " << (func->getDescribedFunctionTemplate() != nullptr) << "\n";
    llvm::outs() << "getPrimaryTemplate               : " << (func->getPrimaryTemplate() != nullptr) << "\n";
    llvm::outs() << "getInstantiatedFromMemberFunction: " << (func->getInstantiatedFromMemberFunction() != nullptr) << "\n";

    llvm::outs() << "isCXXClassMember                 : " << func->isCXXClassMember() << "\n";
    llvm::outs() << "isDefaulted                      : " << func->isDefaulted() << "\n";
    llvm::outs() << "isDeleted                        : " << func->isDeleted() << "\n";
    llvm::outs() << "friendObjectKind(!=None)         : " << (func->getFriendObjectKind() != clang::Decl::FOK_None) << "\n";

    // getTemplatedKind is an enum, not a bool: print it numerically
    llvm::outs() << "getTemplatedKind (enum)          : " << func->getTemplatedKind() << "\n";

    return true;
}

}  // namespace invisible
