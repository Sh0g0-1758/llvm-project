#include "clang/Driver/Options.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/AST/ParentMapContext.h"
#include "llvm/Support/JSON.h"
#include <map>
#include <fstream>
#include <sstream>

using namespace std;
using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

class filterVisitor : public RecursiveASTVisitor<filterVisitor> {
private:
    ASTContext *astContext;
    std::map<std::string, std::string> functions;
    Rewriter* rewriter;
public:
    explicit filterVisitor(ASTContext *C, Rewriter* R)
      : astContext(C), rewriter(R)
    {
        rewriter->setSourceMgr(astContext->getSourceManager(), astContext->getLangOpts());
    }

    virtual bool VisitFunctionDecl(FunctionDecl *func) {
        if (!func->hasBody()) return true;

        std::string funcStr;
        llvm::raw_string_ostream SS(funcStr);

        PrintingPolicy Policy = astContext->getPrintingPolicy();
        Policy.Indentation = 2;

        func->print(SS, Policy);
        SS.flush();

        std::string funcName = func->getNameInfo().getName().getAsString();
        functions[funcName] = SS.str();
        return true;
    }

    void saveFunctionsJSON() {
        SourceManager &SM = rewriter->getSourceMgr();
        FileID mainFileID = SM.getMainFileID();
        auto FERef = SM.getFileEntryRefForID(mainFileID);
        llvm::SmallString<256> outPath = FERef->getName();
        llvm::sys::path::remove_filename(outPath);
        llvm::sys::path::append(outPath, "functions.json");

        llvm::json::Object root;
        for (const auto &entry : functions)
            root[entry.first] = entry.second;

        std::error_code EC;
        llvm::raw_fd_ostream OS(outPath, EC, llvm::sys::fs::OF_Text);
        if (EC) {
            llvm::errs() << "[Invisible] failed to open " << outPath << ": " << EC.message() << "\n";
            return;
        }

        OS << llvm::formatv("{0:2}\n", llvm::json::Value(std::move(root)));
    }
};

void filterCallback(ASTContext &AST, Rewriter* rewriter) {
    filterVisitor *visitor = new filterVisitor(&AST, rewriter);
    visitor->TraverseDecl(AST.getTranslationUnitDecl());
    visitor->saveFunctionsJSON();
}

class inlinePrinterHelper : public PrinterHelper {
    string Prefix;
    string targetVar;
    std::map<int, string> callerParams;
    PrintingPolicy Policy;
public:
    inlinePrinterHelper(string p, string t, std::map<int, string> c, PrintingPolicy pl) : Prefix(p), targetVar(t), callerParams(c), Policy(pl) {}

    virtual bool handledStmt(Stmt *S, llvm::raw_ostream &OS) override {
        if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(S)) {
            if (VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                if (ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(VD)) {
                    unsigned idx = PVD->getFunctionScopeIndex();
                    OS << callerParams[idx];
                } else {
                    OS << Prefix << VD->getNameAsString() << "_" << VD->getID();
                }
                return true;
            }
        }

        if (DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
            for (auto it = DS->decl_begin(); it != DS->decl_end(); ++it) {
                if (VarDecl *VD = dyn_cast<VarDecl>(*it)) {
                    OS << VD->getType().getAsString() << " " 
                    << Prefix << VD->getNameAsString() << "_" << VD->getID();
                    
                    if (VD->hasInit()) {
                        OS << " = ";
                        VD->getInit()->printPretty(OS, this, Policy);
                    }
                    OS << ";\n";
                }
            }
            return true; 
        }

        if (ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
            if (!targetVar.empty()) {
                OS << targetVar << " = ";
                if (Expr *RetVal = RS->getRetValue()) {
                    RetVal->printPretty(OS, this, Policy);
                }
                OS << ";\n";
            } else {
                OS << "[Invisible] no return value\n";
            }
            return true;
        }

        return false;
    }
};

class inlineVisitor : public RecursiveASTVisitor<inlineVisitor> {
private:
    ASTContext *astContext;
    string currentFuncName;
    string source_caller;
    string source_callee;
    Rewriter* rewriter;
public:
    explicit inlineVisitor(ASTContext *C, const std::string& cal_, const std::string& cale_, Rewriter* R) 
      : astContext(C), source_caller(cal_), source_callee(cale_), rewriter(R)
    {
        rewriter->setSourceMgr(astContext->getSourceManager(), astContext->getLangOpts());
    }

    bool isPlainVariable(Expr *arg) {
        Expr *stripped = arg->IgnoreParenImpCasts();

        if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(stripped)) {
            if (isa<VarDecl>(DRE->getDecl())) {
                return true;
            }
        }

        return false;
    }

    virtual bool VisitFunctionDecl(FunctionDecl *func) {
        currentFuncName = func->getNameInfo().getName().getAsString();
        return true;
    }

    virtual bool VisitCallExpr(CallExpr *call) {
        if(source_caller != currentFuncName) return true;
        FunctionDecl *callee = call->getDirectCallee();
        if (!callee || !callee->hasBody()) return true;
        if(source_callee != callee->getNameAsString()) return true;

        ASTContext &Ctx = callee->getASTContext();
        SourceManager &SM = Ctx.getSourceManager();
        const LangOptions &LangOpts = Ctx.getLangOpts();

        string targetVar = "";
        auto parents = Ctx.getParents(*call);
        const Stmt *stmtToReplace = call;

        if (!parents.empty()) {
            auto &ParentNode = parents[0];

            // int x = foo();
            if (const VarDecl *VD = ParentNode.get<VarDecl>()) {
                targetVar = VD->getType().getAsString() + " " + VD->getNameAsString();

                auto grandparents = Ctx.getParents(*VD);
                if (!grandparents.empty()) {
                    if (const DeclStmt *GS = grandparents[0].get<DeclStmt>()) {
                        stmtToReplace = GS;
                    }
                }
            } 
            // x = foo();
            else if (const Stmt *PS = ParentNode.get<Stmt>()) {
                if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(PS)) {
                    if (BO->isAssignmentOp()) {
                        targetVar = Lexer::getSourceText(CharSourceRange::getTokenRange(BO->getLHS()->getSourceRange()), SM, LangOpts).str();
                        stmtToReplace = BO;
                    }
                }
            }
        }

        string inlinedCode;
        string prefix = callee->getNameAsString() + "_" + to_string(callee->getID()) + "_";

        std::map<int, string> callerParams;
        for (unsigned i = 0; i < call->getNumArgs(); ++i) {
            ParmVarDecl *pvd = callee->getParamDecl(i);
            Expr *arg = call->getArg(i);
            string argText = Lexer::getSourceText(
                CharSourceRange::getTokenRange(arg->getSourceRange()), SM, LangOpts).str();

            if (!isPlainVariable(arg)) {
                inlinedCode += pvd->getType().getAsString() + " " + prefix + pvd->getNameAsString() + " = " + argText + ";\n";
                callerParams[i] = prefix + pvd->getNameAsString();
            } else {
                callerParams[i] = argText;
            }
        }

        string bodyStr;
        llvm::raw_string_ostream SS(bodyStr);
        PrintingPolicy Policy = Ctx.getPrintingPolicy();
        Policy.Indentation = 2;

        inlinePrinterHelper Helper(prefix, targetVar, callerParams, Policy);
        callee->getBody()->printPretty(SS, &Helper, Policy);
        string rawBody = SS.str();

        size_t firstBrace = rawBody.find('{');
        size_t lastBrace  = rawBody.rfind('}');

        if (firstBrace != string::npos && lastBrace != string::npos && firstBrace < lastBrace) {
            rawBody = rawBody.substr(firstBrace + 1, lastBrace - firstBrace - 1);
        }

        size_t firstContent = rawBody.find_first_not_of(" \n\r\t");
        size_t lastContent  = rawBody.find_last_not_of(" \n\r\t");

        if (firstContent != string::npos && lastContent != string::npos) {
            rawBody = rawBody.substr(firstContent, lastContent - firstContent + 1);
        }

        inlinedCode += rawBody;

        rewriter->ReplaceText(stmtToReplace->getSourceRange(), inlinedCode);

        return true;
    }
};

void inlineCallback(ASTContext &AST, const std::string& caller, const std::string& callee, Rewriter* rewriter) {
    inlineVisitor *visitor = new inlineVisitor(&AST, caller, callee, rewriter);
    visitor->TraverseDecl(AST.getTranslationUnitDecl());
}

void saveChanges(Rewriter* rewriter) {
    SourceManager &SM = rewriter->getSourceMgr();
    FileID mainFileID = SM.getMainFileID();

    const RewriteBuffer *RewriteBuf = rewriter->getRewriteBufferFor(mainFileID);
    if (RewriteBuf) {
        auto FERef = SM.getFileEntryRefForID(mainFileID);
        llvm::SmallString<256> outPath = FERef->getName();
        llvm::sys::path::remove_filename(outPath);
        llvm::sys::path::append(outPath, "buffer.cpp");

        std::error_code EC;
        llvm::raw_fd_ostream outFile(outPath, EC);


        if (!EC) {
            outFile << string(RewriteBuf->begin(), RewriteBuf->end());
            errs() << "[Invisible] Successfully emitted updated source to buffer.cpp\n";
        }
    } else {
        errs() << "[Invisible] No changes were made to the source.\n";
    }
}

enum class optPass {
    FILTER_PASS,
    INLINE_PASS
};

using TUCallbackType = std::function<void(ASTContext &)>;

std::unique_ptr<FrontendActionFactory> getFrontendActionFactory(optPass optType, const std::string& caller, const std::string& callee) {
    class ToolASTConsumer : public ASTConsumer {
    public:
        TUCallbackType Callback;
        ToolASTConsumer(TUCallbackType Callback)
            : Callback(std::move(Callback)) {}

        void HandleTranslationUnit(ASTContext &Context) override {
            Callback(Context);
        }
    };

    class ToolASTAction : public ASTFrontendAction {
    public:
        explicit ToolASTAction(TUCallbackType Callback)
            : Callback(std::move(Callback)) {}

    protected:
        std::unique_ptr<clang::ASTConsumer>
        CreateASTConsumer(clang::CompilerInstance &compiler,
                        StringRef /* dummy */) override {
        std::unique_ptr<clang::ASTConsumer> Consumer{
            new ToolASTConsumer(Callback)};
        return Consumer;
        }

    private:
        TUCallbackType Callback;
    };

    class ToolActionFactory : public FrontendActionFactory {
    public:
        ToolActionFactory(TUCallbackType Callback)
            : Callback(std::move(Callback)) {}

        std::unique_ptr<FrontendAction> create() override {
            return std::make_unique<ToolASTAction>(Callback);
        }

    private:
        TUCallbackType Callback;
    };

    return std::make_unique<ToolActionFactory>([optType, caller, callee](ASTContext &AST) {
        if (optType == optPass::FILTER_PASS) {
            Rewriter rewriter;
            filterCallback(AST, &rewriter);
        } else if(optType == optPass::INLINE_PASS) {
            Rewriter rewriter;
            inlineCallback(AST, caller, callee, &rewriter);
            saveChanges(&rewriter);
        }
    });
}

std::unique_ptr<FrontendActionFactory> getFrontendActionFactory(optPass optType) {
    return getFrontendActionFactory(optType, "", "");
}

int main(int argc, const char **argv) {
    // oracle gives us the function to be inlined in topo sort for now
    std::vector<std::pair<std::string, std::string>> inline_oracle;
    std::ifstream infile("oracle_list.txt"); // Name of your input file

    if (!infile.is_open()) {
        llvm::errs() << "Error: Could not open oracle file.\n";
    } else {
        std::string line;
        while (std::getline(infile, line)) {
            std::stringstream ss(line);
            std::string caller, callee;
            if (ss >> caller >> callee) {
                inline_oracle.emplace_back(caller, callee);
            }
        }
    }

    // iterate over the oracle and inline functions as required
    for(size_t i = 0; i < inline_oracle.size(); i++) {
        int _argc;
        const char **_argv;
        if(i == 0) {
            _argc = argc; _argv = argv;
        } else {
            static const char* dummy_argv[] = {"", "buffer.cpp"};
            _argc = 2; _argv = dummy_argv;
        }
        auto ExpectedParser = CommonOptionsParser::create(_argc, _argv, cl::getGeneralCategory(), cl::ZeroOrMore, "optimization passes on the source code without tackling IR");
        if (!ExpectedParser) {
            llvm::errs() << ExpectedParser.takeError();
            return 1;
        }
        CommonOptionsParser &Options = ExpectedParser.get();
        ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());
        auto ActionFactory = getFrontendActionFactory(optPass::INLINE_PASS, inline_oracle[i].first, inline_oracle[i].second);
        int result = Tool.run(ActionFactory.get());
    }

    // emit each function in a different file
    {
        int _argc = 2; static const char* _argv[] = {"", "buffer.cpp"};
        auto ExpectedParser = CommonOptionsParser::create(_argc, _argv, cl::getGeneralCategory(), cl::ZeroOrMore, "optimization passes on the source code without tackling IR");
        if (!ExpectedParser) {
            llvm::errs() << ExpectedParser.takeError();
            return 1;
        }
        CommonOptionsParser &Options = ExpectedParser.get();
        ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());
        auto ActionFactory = getFrontendActionFactory(optPass::FILTER_PASS);
        int result = Tool.run(ActionFactory.get());
    }
}
