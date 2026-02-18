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

class commaInitializationVisitor : public RecursiveASTVisitor<commaInitializationVisitor> {
private:
    ASTContext *astContext;
    Rewriter* rewriter;
public:
    explicit commaInitializationVisitor(ASTContext *C, Rewriter* R)
      : astContext(C), rewriter(R)
    {
        rewriter->setSourceMgr(astContext->getSourceManager(), astContext->getLangOpts());
    }

    virtual bool VisitDeclStmt(DeclStmt *DS) {
        if (DS->isSingleDecl())
            return true;

        SmallVector<const VarDecl *, 8> VarDecls;
        for (const Decl *D : DS->decls()) {
            if (const auto *VD = dyn_cast<VarDecl>(D))
                VarDecls.push_back(VD);
            else
                return true;
        }

        const SourceManager &SM = astContext->getSourceManager();
        const LangOptions &LO = astContext->getLangOpts();
        const VarDecl *FirstVD = VarDecls.front();
        QualType QT = FirstVD->getType();
        std::string TypeStr = QT.getAsString();

        std::string Replacement;
        for (const VarDecl *VD : VarDecls) {
            Replacement += TypeStr + " " + VD->getNameAsString();

            if (VD->hasInit()) {
                Replacement += " = ";
                Replacement += Lexer::getSourceText(
                    CharSourceRange::getTokenRange(VD->getInit()->getSourceRange()),
                    SM, LO
                ).str();
            }

            Replacement += ";\n";
        }

        SourceRange SR = DS->getSourceRange();
        rewriter->ReplaceText(
            CharSourceRange::getTokenRange(SR),
            Replacement
        );

        return true;
    }

};

void commaInitializationCallback(ASTContext &AST, Rewriter* rewriter) {
    commaInitializationVisitor *visitor = new commaInitializationVisitor(&AST, rewriter);
    visitor->TraverseDecl(AST.getTranslationUnitDecl());
}

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
    std::vector<size_t>* callSite;
    size_t currIndx;
    Rewriter* rewriter;
public:
    explicit inlineVisitor(ASTContext *C, const std::string& cal_, const std::string& cale_, std::vector<size_t>* cals_, size_t cind_, Rewriter* R) 
      : astContext(C), source_caller(cal_), source_callee(cale_), callSite(cals_), currIndx(cind_), rewriter(R)
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
        
        SourceLocation loc = call->getExprLoc();
        loc = SM.getSpellingLoc(loc);
        PresumedLoc ploc = SM.getPresumedLoc(loc);

        if((ploc.getLine() != (*callSite)[currIndx])) return true;

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
        string prefix = callee->getNameAsString() + "_" + to_string(callee->getID()) + "_" + to_string(ploc.getLine()) + "_";
        size_t addedLines = 0;

        std::map<int, string> callerParams;
        for (unsigned i = 0; i < call->getNumArgs(); ++i) {
            ParmVarDecl *pvd = callee->getParamDecl(i);
            Expr *arg = call->getArg(i);
            string argText = Lexer::getSourceText(
                CharSourceRange::getTokenRange(arg->getSourceRange()), SM, LangOpts).str();

            if (!isPlainVariable(arg)) {
                inlinedCode += pvd->getType().getAsString() + " " + prefix + pvd->getNameAsString() + " = " + argText + ";\n";
                callerParams[i] = prefix + pvd->getNameAsString();
                addedLines++;
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

        if (firstBrace != string::npos && lastBrace != string::npos && firstBrace < lastBrace)
            rawBody = rawBody.substr(firstBrace + 1, lastBrace - firstBrace - 1);

        size_t firstContent = rawBody.find_first_not_of(" \n\r\t");
        size_t lastContent  = rawBody.find_last_not_of(" \n\r\t");

        if (firstContent != string::npos && lastContent != string::npos)
            rawBody = rawBody.substr(firstContent, lastContent - firstContent + 1);

        addedLines += std::count(rawBody.begin(), rawBody.end(), '\n');
        inlinedCode += rawBody;

        for(size_t i = currIndx + 1; i < (*callSite).size(); i++)
            if((*callSite)[i] > (*callSite)[currIndx]) (*callSite)[i] += addedLines;

        rewriter->ReplaceText(stmtToReplace->getSourceRange(), inlinedCode);

        return true;
    }
};

void inlineCallback(ASTContext &AST, const std::string& caller, const std::string& callee, std::vector<size_t>* callSite, size_t currIndx, Rewriter* rewriter) {
    inlineVisitor *visitor = new inlineVisitor(&AST, caller, callee, callSite, currIndx, rewriter);
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
    COMMA_INITIALIZATION_PASS,
    FILTER_PASS,
    INLINE_PASS
};

using TUCallbackType = std::function<void(ASTContext &)>;

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

std::unique_ptr<FrontendActionFactory> getFrontendActionFactory(optPass optType, const std::string& caller, const std::string& callee, std::vector<size_t>* callSite, size_t currIndx) {
    return std::make_unique<ToolActionFactory>([optType, caller, callee, callSite, currIndx](ASTContext &AST) {
        Rewriter rewriter;
        if(optType == optPass::INLINE_PASS) {
            inlineCallback(AST, caller, callee, callSite, currIndx, &rewriter);
            saveChanges(&rewriter);
        }
    });
}

std::unique_ptr<FrontendActionFactory> getFrontendActionFactory(optPass optType) {
    return std::make_unique<ToolActionFactory>([optType](ASTContext &AST) {
        Rewriter rewriter;
        if (optType == optPass::COMMA_INITIALIZATION_PASS) {
            commaInitializationCallback(AST, &rewriter);
            saveChanges(&rewriter);
        } else if (optType == optPass::FILTER_PASS) {
            filterCallback(AST, &rewriter);
        }
    });
}

int main(int argc, const char **argv) {
    // preprocess step to convert comma initializers to stand-alone statements
    if(llvm::StringRef(argv[2]) == "only-preprocess") {
        int _argc = 2;
        const char* _argv[] = {"", argv[1]};
        auto ExpectedParser = CommonOptionsParser::create(_argc, _argv, cl::getGeneralCategory(), cl::ZeroOrMore, "optimization passes on the source code without tackling IR");
        if (!ExpectedParser) {
            llvm::errs() << ExpectedParser.takeError();
            return 1;
        }
        CommonOptionsParser &Options = ExpectedParser.get();
        ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());
        auto ActionFactory = getFrontendActionFactory(optPass::COMMA_INITIALIZATION_PASS);
        int result = Tool.run(ActionFactory.get());
        return 0;
    }

    // oracle gives us the function to be inlined in topo sort for now
    std::vector<std::pair<std::string, std::string>> inline_oracle;
    std::vector<size_t> inline_debugLoc;
    std::ifstream infile("oracle_list.txt"); // Name of your input file

    if (!infile.is_open()) {
        llvm::errs() << "Error: Could not open oracle file.\n";
    } else {
        std::string line;
        while (std::getline(infile, line)) {
            std::stringstream ss(line);
            std::string caller, callee;
            size_t row;
            if (ss >> caller >> callee >> row) {
                inline_oracle.emplace_back(caller, callee);
                inline_debugLoc.emplace_back(row);
            }
        }
    }

    int _argc = 2;
    const char* _argv[2];

    // iterate over the oracle and inline functions as required
    for(size_t i = 0; i < inline_oracle.size(); i++) {
        _argv[1] = i == 0 ? argv[1] : "buffer.cpp";
        auto ExpectedParser = CommonOptionsParser::create(_argc, _argv, cl::getGeneralCategory(), cl::ZeroOrMore, "optimization passes on the source code without tackling IR");
        if (!ExpectedParser) {
            llvm::errs() << ExpectedParser.takeError();
            return 1;
        }
        CommonOptionsParser &Options = ExpectedParser.get();
        ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());
        auto ActionFactory = getFrontendActionFactory(optPass::INLINE_PASS, inline_oracle[i].first, inline_oracle[i].second, &inline_debugLoc, i);
        int result = Tool.run(ActionFactory.get());
    }

    // emit each function in a different file
    {
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
