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
#include <cstdlib>

using namespace std;
using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

class dummyPrinterHelper : public PrinterHelper {
public:
    dummyPrinterHelper() {}
    virtual bool handledStmt(Stmt *S, llvm::raw_ostream &OS) override {
        return false;
    }
};

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

    /* use for debugging */
    // virtual bool VisitCallExpr(CallExpr *call) {
    //     FunctionDecl *callee = call->getDirectCallee();
    //     if (!callee || !callee->hasBody()) return true;
    //     if (auto *memberCall = dyn_cast<CXXMethodDecl>(callee)) {return true;}
    //     llvm::outs() << callee->getNameAsString() << "\n";
    //     if (callee->isFunctionTemplateSpecialization()) llvm::outs() << "found a template with function body\n";
    //     string bodyStr;
    //     llvm::raw_string_ostream SS(bodyStr);
    //     PrintingPolicy Policy = astContext->getPrintingPolicy();
    //     Policy.Indentation = 2;

    //     dummyPrinterHelper Helper;
    //     callee->getBody()->printPretty(SS, &Helper, Policy);
    //     string rawBody = SS.str();
    //     llvm::outs() << rawBody << "\n";
    //     return true;
    // }

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

        clang::PrintingPolicy policy(astContext->getLangOpts());
        policy.adjustForCPlusPlus();

        std::string Replacement;
        for (const VarDecl *VD : VarDecls) {
            std::string s;
            llvm::raw_string_ostream os(s);
            VD->print(os, policy);
            Replacement += s + ";\n";

            // QualType qt = VD->getType();
            // std::string array_suffix = "";

            // // array type
            // while (const auto* array_type = astContext->getAsConstantArrayType(qt)) {
            //     llvm::SmallString<16> size_str;
            //     array_type->getSize().toString(size_str, 10, false);
            //     array_suffix += "[" + std::string(size_str) + "]";
            //     qt = array_type->getElementType();
            // }
            
            // // pointer type
            // if (const auto* ptr_type = qt->getAs<clang::PointerType>()) {
            //     if (const auto* func_type = ptr_type->getPointeeType()->getAs<clang::FunctionProtoType>()) {
            //         Replacement += func_type->getReturnType().getAsString(policy);
            //         Replacement += " (* " + qt.getQualifiers().getAsString(policy) + " " + VD->getNameAsString() + array_suffix + ")";
            //         Replacement += "(";
            //         for(size_t i = 0; i < func_type->getParamTypes().size(); i++) {
            //             Replacement += func_type->getParamTypes()[i].getAsString(policy);
            //             if(i != func_type->getParamTypes().size() - 1) Replacement += ",";
            //         }
            //         Replacement += ")";
            //     } else {
            //         Replacement += ptr_type->getPointeeType().getAsString(policy) + " *" + qt.getQualifiers().getAsString(policy) + " " + VD->getNameAsString() + array_suffix;
            //     }
            // } else {
            //     Replacement += qt.getAsString(policy) + " " + VD->getNameAsString() + array_suffix;
            // }

            // if (VD->hasInit()) {
            //     Replacement += " = ";
            //     Replacement += Lexer::getSourceText(
            //         CharSourceRange::getTokenRange(VD->getInit()->getSourceRange()),
            //         SM, LO
            //     ).str();
            // }

            // Replacement += ";\n";
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

bool FuncCallCleaned;

class cleanupFuncCallVisitor : public RecursiveASTVisitor<cleanupFuncCallVisitor> {
private:
    ASTContext *astContext;
    Rewriter* rewriter;
public:
    explicit cleanupFuncCallVisitor(ASTContext *C, Rewriter* R)
      : astContext(C), rewriter(R)
    {
        rewriter->setSourceMgr(astContext->getSourceManager(), astContext->getLangOpts());
        FuncCallCleaned = false;
    }

    SourceLocation findInsertionLocBeforeCompound(const CallExpr *call) {
        DynTypedNode current = DynTypedNode::create(*call);
        const Stmt *lastStmt = nullptr;

        while (true) {
            auto parents = astContext->getParents(current);
            if (parents.empty())
                return SourceLocation();

            const DynTypedNode &parent = parents[0];
            if (parent.get<CompoundStmt>()) {
                if (!lastStmt)
                    return SourceLocation();
                return lastStmt->getBeginLoc();
            }

            if (const Stmt *S = parent.get<Stmt>()) {
                lastStmt = S;
            }

            current = parent;
        }
    }

    // This is supposed to run after doing a clang-format --style="{InsertBraces: true}". Hence if a call is good,
    // ie. of the form int x = foo() | x = foo() | foo(), its parent must be a CompoundStmt. 
    // This is not true for If/While/For/Switch statements. But in them, the function that return something should only be called. 
    // again this is not true only for ForStmt, in which the first argument can be a function call that returns nothing.
    virtual bool VisitCallExpr(CallExpr *call) {
        if (FuncCallCleaned) return true;
        FunctionDecl *callee = call->getDirectCallee();
        bool goodCall = false;
        auto parents = astContext->getParents(*call);
        if (!parents.empty()) {
            auto &ParentNode = parents[0];

            // int x = foo();
            if (const VarDecl *VD = ParentNode.get<VarDecl>()) {
                // make sure its at the top level and not nested like for (int i = foo(base); i < 20; i++)
                auto parents = astContext->getParents(*VD);
                auto& ParentNode = parents[0];
                if(const DeclStmt* DS = ParentNode.get<DeclStmt>()) {
                    auto parents = astContext->getParents(*DS);
                    auto& ParentNode = parents[0];
                    if (const Stmt *PS = ParentNode.get<Stmt>()) {
                        if(dyn_cast<CompoundStmt>(PS)) {
                            goodCall = true;
                        }
                    }
                }
            } 
            else if (const Stmt *PS = ParentNode.get<Stmt>()) {
                // x = foo();
                if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(PS)) {
                    // make sure its at the top level and not nested like if (45 == (a = foo()))
                    if (BO->isAssignmentOp()) {
                        auto parents = astContext->getParents(*BO);
                        auto& ParentNode = parents[0];
                        if (const Stmt *PS = ParentNode.get<Stmt>()) {
                            if(dyn_cast<CompoundStmt>(PS)) {
                                goodCall = true;
                            }
                        }
                    }
                // foo();
                } else if (dyn_cast<CompoundStmt>(PS)) {
                    goodCall = true;
                }
            }
        }

        if (goodCall) return true;
        FuncCallCleaned = true;

        SourceManager &SM = astContext->getSourceManager();
        auto LangOpts = astContext->getLangOpts();

        auto call_ = Lexer::getSourceText(CharSourceRange::getTokenRange(call->getSourceRange()), SM, LangOpts).str();

        std::string codeToAdd = "";
        std::string retVar    = "";
        if(call->getType()->isVoidType()) {
            codeToAdd += call_ + ";\n";
        } else {
            QualType retTy = callee->getReturnType();

            PrintingPolicy policy(astContext->getLangOpts());
            policy.adjustForCPlusPlus();
            SourceLocation loc = call->getExprLoc();
            loc = SM.getSpellingLoc(loc);
            PresumedLoc ploc = SM.getPresumedLoc(loc);

            retVar = callee->getNameAsString() + "_" + to_string(callee->getID()) + "_" + to_string(ploc.getLine()) + "_" + to_string(ploc.getColumn()) + "_" + "Ret";
            llvm::raw_string_ostream OS(codeToAdd);
            retTy.print(OS, policy, retVar);

            codeToAdd += " = " + call_ + ";\n";
        }

        rewriter->InsertTextBefore(findInsertionLocBeforeCompound(call), codeToAdd);
        rewriter->ReplaceText(call->getSourceRange(), retVar);
        return true;
    }
};

void cleanupFuncCallCallback(ASTContext &AST, Rewriter* rewriter) {
    cleanupFuncCallVisitor *visitor = new cleanupFuncCallVisitor(&AST, rewriter);
    visitor->TraverseDecl(AST.getTranslationUnitDecl());
}

bool constantFolded;

class constantFoldVisitor : public RecursiveASTVisitor<constantFoldVisitor> {
private:
    ASTContext *astContext;
    Rewriter* rewriter;
public:
    explicit constantFoldVisitor(ASTContext *C, Rewriter* R)
      : astContext(C), rewriter(R)
    {
        rewriter->setSourceMgr(astContext->getSourceManager(), astContext->getLangOpts());
        constantFolded = false;
    }

    union operandVal {
        uint64_t i;
        double   f;
    };

    bool getOperands(Expr* operand, operandVal& op) {
        if(auto* ICE = dyn_cast<ImplicitCastExpr>(operand)) {
            CastKind CK = ICE->getCastKind();
            if(CK == CK_IntegralCast || CK == CK_FloatingCast) {
                return getOperands(ICE->getSubExpr()->IgnoreParenNoopCasts(*astContext), op);
            } else if(CK == CK_FloatingToIntegral) {
                auto res = getOperands(ICE->getSubExpr()->IgnoreParenNoopCasts(*astContext), op);
                if(!res) return false;
                op.i = (uint64_t)op.f;
                return true;
            } else if(CK == CK_IntegralToFloating) {
                auto res = getOperands(ICE->getSubExpr()->IgnoreParenNoopCasts(*astContext), op);
                if(!res) return false;
                if(ICE->getSubExpr()->getType()->isSignedIntegerType()) op.f = (double)((int64_t)op.i);
                else op.f = (double)op.i;
                return true;
            } else if(CK == CK_IntegralToBoolean) {
                auto res = getOperands(ICE->getSubExpr()->IgnoreParenNoopCasts(*astContext), op);
                if(!res) return false;
                op.i = (uint64_t)((bool)op.i);
                return true;
            } else if(CK == CK_FloatingToBoolean) {
                auto res = getOperands(ICE->getSubExpr()->IgnoreParenNoopCasts(*astContext), op);
                if(!res) return false;
                op.i = (uint64_t)((bool)op.f);
                return true;
            }
        }

        if(auto* UO = dyn_cast<UnaryOperator>(operand)) {
            UnaryOperator::Opcode uop = UO->getOpcode();
            if(uop == UnaryOperator::Opcode::UO_Minus) {
                auto res = getOperands(UO->getSubExpr()->IgnoreParenNoopCasts(*astContext), op);
                if(!res) return false;
                if(UO->getType()->getAs<BuiltinType>()->isFloatingPoint())        op.f = -op.f;
                else if(UO->getType()->getAs<BuiltinType>()->isUnsignedInteger()) op.i = -op.i;
                else if(UO->getType()->getAs<BuiltinType>()->isSignedInteger())   op.i = -(int64_t)op.i;
                return true;
            } else if(uop == UnaryOperator::Opcode::UO_Plus) {
                return getOperands(UO->getSubExpr()->IgnoreParenNoopCasts(*astContext), op);
            }
        }

        if(auto* IL = dyn_cast<IntegerLiteral>(operand)) {
            op.i = IL->getType()->getAs<BuiltinType>()->isSignedInteger() ?
                   (uint64_t)IL->getValue().getSExtValue() :
                   IL->getValue().getZExtValue();
            return true;
        } else if(auto* FL = dyn_cast<FloatingLiteral>(operand)) {
            op.f = FL->getValue().convertToDouble();
            return true;
        } else if(auto* CL = dyn_cast<CharacterLiteral>(operand)) {
            op.i = (uint64_t)CL->getValue();
            return true;
        } else if(auto* BL = dyn_cast<CXXBoolLiteralExpr>(operand)) {
            op.i = (uint64_t)BL->getValue();
            return true;
        }

        return false;
    }

    virtual bool VisitUnaryOperator(UnaryOperator* UO) {
        if(UO->getOpcode() == UnaryOperator::Opcode::UO_Minus || UO->getOpcode() == UnaryOperator::Opcode::UO_Plus) return true;
        if(constantFolded) return true;
        operandVal rhs;
        if(getOperands(UO->getSubExpr()->IgnoreParenNoopCasts(*astContext), rhs)) {
            auto* UT = UO->getType()->getAs<BuiltinType>();
            if(UT->isUnsignedInteger()) {
                bool valid = true;
                uint64_t result = 0;
                uint64_t rval = rhs.i;
                switch(UO->getOpcode()) {
                    case UnaryOperator::Opcode::UO_Not: result = ~rval; break;
                    case UnaryOperator::Opcode::UO_LNot: result = !rval; break;
                    default: valid = false; break;
                }

                if (valid) {
                    constantFolded = true;
                    uint64_t width = astContext->getTypeSize(UO->getType());
                    uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
                    result &= mask;
                    llvm::APInt ap_result(width, result, false);
                    llvm::SmallString<32> str;
                    ap_result.toString(str, 10, false);
                    rewriter->ReplaceText(UO->getSourceRange(), str.str().str());
                }
            } else if(UT->isSignedInteger()) {
                bool valid = true;
                int64_t result = 0;
                int64_t rval = rhs.i;
                switch(UO->getOpcode()) {
                    case UnaryOperator::Opcode::UO_Not: result = ~rval; break;
                    case UnaryOperator::Opcode::UO_LNot: result = !rval; break;
                    default: valid = false; break;
                }

                if (valid) {
                    constantFolded = true;
                    uint64_t width = astContext->getTypeSize(UO->getType());
                    uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);

                    int64_t truncated = (int64_t)(result & mask);
                    if (width < 64 && (truncated & (1LL << (width - 1))))
                        truncated |= ~mask;

                    llvm::APInt ap_result(width, (uint64_t)truncated, true);
                    llvm::SmallString<32> str;
                    ap_result.toString(str, 10, true);
                    rewriter->ReplaceText(UO->getSourceRange(), str.str().str());
                }
            } else if(UT->isFloatingPoint()) {
                bool valid = true;
                double result = 0;
                double rval = rhs.i;
                switch(UO->getOpcode()) {
                    case UnaryOperator::Opcode::UO_LNot: result = !rval; break;
                    default: valid = false; break;
                }

                if (valid) {
                    constantFolded = true;
                    if (UT->getKind() == BuiltinType::Float)
                        rewriter->ReplaceText(UO->getSourceRange(), std::to_string((float)result));
                    else
                        rewriter->ReplaceText(UO->getSourceRange(), std::to_string(result));
                }
            }
        }
        return true;
    }

    // what about += | -= | *= | /= ?
    virtual bool VisitBinaryOperator(BinaryOperator *BO) {
        if(constantFolded) return true;
        operandVal lhs;
        operandVal rhs;
        if(getOperands(BO->getLHS()->IgnoreParenNoopCasts(*astContext), lhs) && getOperands(BO->getRHS()->IgnoreParenNoopCasts(*astContext), rhs)) {
            auto* BT = BO->getType()->getAs<BuiltinType>();
            if(BT->isUnsignedInteger()) {
                bool valid = true;
                uint64_t result = 0;
                uint64_t lval = lhs.i;
                uint64_t rval = rhs.i;
                switch(BO->getOpcode()) {
                    // arith
                    case BinaryOperator::Opcode::BO_Add:  result = lval + rval; break;
                    case BinaryOperator::Opcode::BO_Sub:  result = lval - rval; break;
                    case BinaryOperator::Opcode::BO_Mul:  result = lval * rval; break;
                    case BinaryOperator::Opcode::BO_Div:
                        if (rval == 0) { valid = false; break; }
                        result = lval / rval; break;
                    case BinaryOperator::Opcode::BO_Rem:
                        if (rval == 0) { valid = false; break; }
                        result = lval % rval; break;
    
                    // Bitwise
                    case BinaryOperator::Opcode::BO_And:  result = lval & rval;  break;
                    case BinaryOperator::Opcode::BO_Or:   result = lval | rval;  break;
                    case BinaryOperator::Opcode::BO_Xor:  result = lval ^ rval;  break;
                    case BinaryOperator::Opcode::BO_Shl:  result = lval << rval; break;
                    case BinaryOperator::Opcode::BO_Shr:  result = lval >> rval; break;
    
                    // Logical/Comparison (return 0 or 1)
                    case BinaryOperator::Opcode::BO_LAnd: result = lval && rval; break;
                    case BinaryOperator::Opcode::BO_LOr:  result = lval || rval; break;
                    case BinaryOperator::Opcode::BO_LT:   result = lval <  rval; break;
                    case BinaryOperator::Opcode::BO_GT:   result = lval >  rval; break;
                    case BinaryOperator::Opcode::BO_LE:   result = lval <= rval; break;
                    case BinaryOperator::Opcode::BO_GE:   result = lval >= rval; break;
                    case BinaryOperator::Opcode::BO_EQ:   result = lval == rval; break;
                    case BinaryOperator::Opcode::BO_NE:   result = lval != rval; break;
    
                    default: valid = false; break;
                }
    
                if (valid) {
                    constantFolded = true;
                    uint64_t width = astContext->getTypeSize(BO->getType());
                    uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
                    result &= mask;
                    llvm::APInt ap_result(width, result, false);
                    llvm::SmallString<32> str;
                    ap_result.toString(str, 10, false);
                    rewriter->ReplaceText(BO->getSourceRange(), str.str().str());
                }
            } else if(BT->isSignedInteger()) {
                bool valid = true;
                int64_t result = 0;
                int64_t lval = lhs.i;
                int64_t rval = rhs.i;
                switch(BO->getOpcode()) {
                    // arith
                    case BinaryOperator::Opcode::BO_Add:  result = lval + rval; break;
                    case BinaryOperator::Opcode::BO_Sub:  result = lval - rval; break;
                    case BinaryOperator::Opcode::BO_Mul:  result = lval * rval; break;
                    case BinaryOperator::Opcode::BO_Div:
                        if (rval == 0) { valid = false; break; }
                        result = lval / rval; break;
                    case BinaryOperator::Opcode::BO_Rem:
                        if (rval == 0) { valid = false; break; }
                        result = lval % rval; break;
    
                    // Bitwise
                    case BinaryOperator::Opcode::BO_And:  result = lval & rval;  break;
                    case BinaryOperator::Opcode::BO_Or:   result = lval | rval;  break;
                    case BinaryOperator::Opcode::BO_Xor:  result = lval ^ rval;  break;
                    case BinaryOperator::Opcode::BO_Shl:  result = lval << rval; break;
                    case BinaryOperator::Opcode::BO_Shr:  result = lval >> rval; break;
    
                    // Logical/Comparison (return 0 or 1)
                    case BinaryOperator::Opcode::BO_LAnd: result = lval && rval; break;
                    case BinaryOperator::Opcode::BO_LOr:  result = lval || rval; break;
                    case BinaryOperator::Opcode::BO_LT:   result = lval <  rval; break;
                    case BinaryOperator::Opcode::BO_GT:   result = lval >  rval; break;
                    case BinaryOperator::Opcode::BO_LE:   result = lval <= rval; break;
                    case BinaryOperator::Opcode::BO_GE:   result = lval >= rval; break;
                    case BinaryOperator::Opcode::BO_EQ:   result = lval == rval; break;
                    case BinaryOperator::Opcode::BO_NE:   result = lval != rval; break;
    
                    default: valid = false; break;
                }
    
                if (valid) {
                    constantFolded = true;
                    uint64_t width = astContext->getTypeSize(BO->getType());
                    uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);

                    int64_t truncated = (int64_t)(result & mask);
                    if (width < 64 && (truncated & (1LL << (width - 1))))
                        truncated |= ~mask;

                    llvm::APInt ap_result(width, (uint64_t)truncated, true);
                    llvm::SmallString<32> str;
                    ap_result.toString(str, 10, true);
                    rewriter->ReplaceText(BO->getSourceRange(), str.str().str());
                }
            } else if (BT->isFloatingPoint()) {
                double lval = lhs.f;
                double rval = rhs.f;
    
                double result = 0.0;
                bool valid = true;
                bool isComparison = false;
                int64_t cmpResult = 0;
    
                switch(BO->getOpcode()) {
                    // Arithmetic
                    case BinaryOperator::Opcode::BO_Add: result = lval + rval; break;
                    case BinaryOperator::Opcode::BO_Sub: result = lval - rval; break;
                    case BinaryOperator::Opcode::BO_Mul: result = lval * rval; break;
                    case BinaryOperator::Opcode::BO_Div:
                        if (rval == 0.0) { valid = false; break; }
                        result = lval / rval; break;
                    case BinaryOperator::Opcode::BO_Rem:
                        if (rval == 0.0) { valid = false; break; }
                        result = std::fmod(lval, rval); break;
    
                    // Comparisons (produce an integer 0 or 1, not a float)
                    case BinaryOperator::Opcode::BO_LAnd: isComparison = true; cmpResult = lval && rval; break;
                    case BinaryOperator::Opcode::BO_LOr:  isComparison = true; cmpResult = lval || rval; break;
                    case BinaryOperator::Opcode::BO_LT:   isComparison = true; cmpResult = lval <  rval; break;
                    case BinaryOperator::Opcode::BO_GT:   isComparison = true; cmpResult = lval >  rval; break;
                    case BinaryOperator::Opcode::BO_LE:   isComparison = true; cmpResult = lval <= rval; break;
                    case BinaryOperator::Opcode::BO_GE:   isComparison = true; cmpResult = lval >= rval; break;
                    case BinaryOperator::Opcode::BO_EQ:   isComparison = true; cmpResult = lval == rval; break;
                    case BinaryOperator::Opcode::BO_NE:   isComparison = true; cmpResult = lval != rval; break;
    
                    default: valid = false; break;
                }

                if (valid) {
                    constantFolded = true;
                    if(isComparison) {
                        rewriter->ReplaceText(BO->getSourceRange(), std::to_string(cmpResult));
                    } else {
                        if (BT->getKind() == BuiltinType::Float) {
                            rewriter->ReplaceText(BO->getSourceRange(), std::to_string((float)result));
                        } else {
                            rewriter->ReplaceText(BO->getSourceRange(), std::to_string(result));
                        }
                    }
                }
            }
        }
        return true;
    }

    virtual bool VisitConditionalOperator(ConditionalOperator* CO) {
        if(constantFolded) return true;
        operandVal lhs;
        operandVal rhs;
        operandVal cond;
        if(getOperands(CO->getLHS()->IgnoreParenNoopCasts(*astContext), lhs) && getOperands(CO->getRHS()->IgnoreParenNoopCasts(*astContext), rhs) && getOperands(CO->getCond()->IgnoreParenNoopCasts(*astContext), cond)) {
            auto* CT = CO->getType()->getAs<BuiltinType>();
            if(CT->isUnsignedInteger()) {
                uint64_t result = 0;
                uint64_t lval = lhs.i;
                uint64_t rval = rhs.i;
                uint64_t condval = cond.i;
                result = condval ? lval : rval; 
                {
                    constantFolded = true;
                    uint64_t width = astContext->getTypeSize(CO->getType());
                    uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
                    result &= mask;
                    llvm::APInt ap_result(width, result, false);
                    llvm::SmallString<32> str;
                    ap_result.toString(str, 10, false);
                    rewriter->ReplaceText(CO->getSourceRange(), str.str().str());
                }
            } else if(CT->isSignedInteger()) {
                int64_t result = 0;
                int64_t lval = lhs.i;
                int64_t rval = rhs.i;
                uint64_t condval = cond.i;
                result = condval ? lval : rval;
                {
                    constantFolded = true;
                    uint64_t width = astContext->getTypeSize(CO->getType());
                    uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);

                    int64_t truncated = (int64_t)(result & mask);
                    if (width < 64 && (truncated & (1LL << (width - 1))))
                        truncated |= ~mask;

                    llvm::APInt ap_result(width, (uint64_t)truncated, true);
                    llvm::SmallString<32> str;
                    ap_result.toString(str, 10, true);
                    rewriter->ReplaceText(CO->getSourceRange(), str.str().str());
                }
            } else if(CT->isFloatingPoint()) {
                double result = 0;
                double lval = lhs.f;
                double rval = rhs.f;
                uint64_t condval = cond.i;
                result = condval ? lval : rval;
                {
                    constantFolded = true;
                    if (CT->getKind() == BuiltinType::Float) {
                        rewriter->ReplaceText(CO->getSourceRange(), std::to_string((float)result));
                    } else {
                        rewriter->ReplaceText(CO->getSourceRange(), std::to_string(result));
                    }
                }
            }
        }
        return true;
    }
};

void constantFoldCallback(ASTContext &AST, Rewriter* rewriter) {
    constantFoldVisitor *visitor = new constantFoldVisitor(&AST, rewriter);
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

    struct VLARenamer : clang::RecursiveASTVisitor<VLARenamer> {
        const std::string &Prefix;
        clang::ASTContext &Ctx;
        std::vector<std::pair<clang::VarDecl*, clang::IdentifierInfo*>> &renamed;
        std::map<int, string> &callerParams;

        VLARenamer(const std::string &P, clang::ASTContext &C,
                std::vector<std::pair<clang::VarDecl*, clang::IdentifierInfo*>> &R, std::map<int, string> &PR)
            : Prefix(P), Ctx(C), renamed(R), callerParams(PR) {}

        bool VisitDeclRefExpr(clang::DeclRefExpr *DRE) {
            if (clang::VarDecl *VD = llvm::dyn_cast<clang::VarDecl>(DRE->getDecl())) {
                std::string newName{};
                if (clang::ParmVarDecl *PVD = dyn_cast<clang::ParmVarDecl>(VD)) {
                    unsigned idx = PVD->getFunctionScopeIndex();
                    newName = callerParams[idx];
                } else {
                    std::string newName = Prefix + VD->getNameAsString() + "_" + std::to_string(VD->getID());
                }
                clang::IdentifierInfo *oldII = VD->getIdentifier();
                renamed.push_back({VD, oldII});
                VD->setDeclName(clang::DeclarationName(&Ctx.Idents.get(newName)));
            }
            return true;
        }
    };

    void renameVLASizeVars(clang::QualType T, const std::string &Prefix, clang::ASTContext &Ctx,
                        std::vector<std::pair<clang::VarDecl*, clang::IdentifierInfo*>> &renamed) {
        const clang::Type *TP = T.getCanonicalType().getTypePtr();

        if (const auto *VAT = llvm::dyn_cast<clang::VariableArrayType>(TP)) {
            if (clang::Expr *size = VAT->getSizeExpr()) {
                VLARenamer r(Prefix, Ctx, renamed, callerParams);
                r.TraverseStmt(size);
            }
            renameVLASizeVars(VAT->getElementType(), Prefix, Ctx, renamed);
        } else if (const auto *DSAT = llvm::dyn_cast<clang::DependentSizedArrayType>(TP)) {
            if (clang::Expr *size = DSAT->getSizeExpr()) {
                VLARenamer r(Prefix, Ctx, renamed, callerParams);
                r.TraverseStmt(size);
            }
            renameVLASizeVars(DSAT->getElementType(), Prefix, Ctx, renamed);
        } else if (const auto *PT = llvm::dyn_cast<clang::PointerType>(TP)) {
            renameVLASizeVars(PT->getPointeeType(), Prefix, Ctx, renamed);
        } else if (const auto *RT = llvm::dyn_cast<clang::ReferenceType>(TP)) {
            renameVLASizeVars(RT->getPointeeType(), Prefix, Ctx, renamed);
        } else if (const auto *AT = llvm::dyn_cast<clang::ArrayType>(TP)) {
            renameVLASizeVars(AT->getElementType(), Prefix, Ctx, renamed);
        } else if (const auto *FPT = llvm::dyn_cast<clang::FunctionProtoType>(TP)) {
            renameVLASizeVars(FPT->getReturnType(), Prefix, Ctx, renamed);
            for (auto paramType : FPT->param_types())
                renameVLASizeVars(paramType, Prefix, Ctx, renamed);
        } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
            renameVLASizeVars(FT->getReturnType(), Prefix, Ctx, renamed);
        } else if (const auto *FT = llvm::dyn_cast<clang::FunctionNoProtoType>(TP)) {
            renameVLASizeVars(FT->getReturnType(), Prefix, Ctx, renamed);
        } else if (const auto *MPT = llvm::dyn_cast<clang::MemberPointerType>(TP)) {
            renameVLASizeVars(MPT->getPointeeType(), Prefix, Ctx, renamed);
        } else if (const auto *VT = llvm::dyn_cast<clang::VectorType>(TP)) {
            renameVLASizeVars(VT->getElementType(), Prefix, Ctx, renamed);
        } else if (const auto *ParenT = llvm::dyn_cast<clang::ParenType>(TP)) {
            renameVLASizeVars(ParenT->getInnerType(), Prefix, Ctx, renamed);
        }
    }

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
                    ASTContext &Ctx = VD->getASTContext();
                    std::string new_name = Prefix + VD->getNameAsString() + "_" + std::to_string(VD->getID());

                    IdentifierInfo *oldII = VD->getIdentifier();
                    VD->setDeclName(DeclarationName(&Ctx.Idents.get(new_name)));

                    std::vector<std::pair<VarDecl*, IdentifierInfo*>> sizeRenames;
                    renameVLASizeVars(VD->getType(), Prefix, Ctx, sizeRenames);

                    Expr *init = VD->getInit();
                    VD->setInit(nullptr);

                    VD->print(OS, Policy);

                    VD->setInit(init);
                    VD->setDeclName(DeclarationName(oldII));
                    for (auto &[vd, ii] : sizeRenames)
                        vd->setDeclName(DeclarationName(ii));

                    if (init) {
                        switch (VD->getInitStyle()) {
                            case VarDecl::CInit:
                                OS << " = ";
                                init->printPretty(OS, this, Policy);
                                OS << ";\n";
                                break;
                            case VarDecl::CallInit:
                                OS << "(";
                                init->printPretty(OS, this, Policy);
                                OS << ");\n";
                                break;
                            case VarDecl::ListInit:
                                init->printPretty(OS, this, Policy);
                                OS << ";\n";
                                break;
                            case VarDecl::ParenListInit:
                                OS  << "(";
                                init->printPretty(OS, this, Policy);
                                OS << ");\n";
                                break;
                        }
                    } else {
                        OS << ";\n";
                    }
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
    std::vector<std::pair<size_t, std::string>>* callSite;
    size_t currIndx;
    Rewriter* rewriter;
public:
    explicit inlineVisitor(ASTContext *C, const std::string& cal_, const std::string& cale_, std::vector<std::pair<size_t, std::string>>* cals_, size_t cind_, Rewriter* R) 
      : astContext(C), source_caller(cal_), source_callee(cale_), callSite(cals_), currIndx(cind_), rewriter(R)
    {
        rewriter->setSourceMgr(astContext->getSourceManager(), astContext->getLangOpts());
    }

    // use this to identify if we are dealing with an lvalue or not. 
    // currently not being used as we can simply do some liveness analysis later to take care of this. 
    bool isPlainVariable(Expr *arg) {
        Expr *stripped = arg->IgnoreParenImpCasts();

        if (auto *construct = dyn_cast<CXXConstructExpr>(stripped)) {
            if (construct->getNumArgs() == 1) {
                stripped = construct->getArg(0)->IgnoreParenImpCasts();
            }
        }

        if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(stripped)) {
            if (DRE->isLValue()) {
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
        // TODO: Handle class methods. We behave conservatively towards them as they might 
        //       be using class private vars which would need special treatment later on. 
        if (auto *memberCall = dyn_cast<CXXMethodDecl>(callee)) {return true;}
        if(source_callee != callee->getNameAsString()) return true;

        ASTContext &Ctx = callee->getASTContext();
        SourceManager &SM = Ctx.getSourceManager();
        
        SourceLocation loc = call->getExprLoc();
        loc = SM.getSpellingLoc(loc);
        PresumedLoc ploc = SM.getPresumedLoc(loc);

        if((ploc.getLine() != (*callSite)[currIndx].first)) return true;

        const LangOptions &LangOpts = Ctx.getLangOpts();
        PrintingPolicy Policy = Ctx.getPrintingPolicy();
        Policy.adjustForCPlusPlus();
        Policy.Indentation = 2;

        string targetVar{};
        auto parents = Ctx.getParents(*call);
        const Stmt *stmtToReplace = call;

        string inlinedCode{};
        llvm::raw_string_ostream OS(inlinedCode);
        size_t addedLines = 0;
        if (!parents.empty()) {
            // xvalue cleanup
            if (const ExprWithCleanups *EWC = parents[0].get<ExprWithCleanups>()) {
                parents = Ctx.getParents(*EWC);
            }

            auto &ParentNode = parents[0];

            // int x = foo();
            // TODO: This might need VLA size specifiers as well
            if (const VarDecl *VD = ParentNode.get<VarDecl>()) {
                Expr *init = VD->getInit();
                VD->setInit(nullptr);
                VD->print(OS, Policy);
                VD->setInit(init);
                OS << ";\n";
                addedLines += 1;
                targetVar = VD->getNameAsString();

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

        string calleeName = callee->getNameAsString();
        // remove the operator bit (+/-/*) from the callee name
        if(dyn_cast<CXXOperatorCallExpr>(call)) calleeName = calleeName.substr(0, calleeName.size() - 2);
        string prefix = calleeName + "_" + to_string(callee->getID()) + "_" + to_string(ploc.getLine()) + "_";

        std::map<int, string> callerParams;
        for (unsigned i = 0; i < call->getNumArgs(); ++i) {
            ParmVarDecl *pvd = callee->getParamDecl(i);
            Expr *arg = call->getArg(i);

            string argText;
            if (auto *DAE = dyn_cast<CXXDefaultArgExpr>(arg)) {
                llvm::raw_string_ostream DS(argText);
                DAE->getExpr()->printPretty(DS, nullptr, Policy);
                DS.flush();
            } else {
                llvm::raw_string_ostream SS(argText);
                arg->printPretty(SS, nullptr, Policy);
                SS.flush();
            }

            if (pvd->getType()->isLValueReferenceType()) {
                callerParams[i] = argText;
            } else {
                // TODO: we won't be handling storage specifiers. Though only register would ever be there
                callerParams[i] = prefix + pvd->getNameAsString();
                pvd->getType().print(OS, Policy, callerParams[i]);
                OS << " = " << argText << ";\n";
                addedLines++;
            }
        }

        string bodyStr;
        llvm::raw_string_ostream SS(bodyStr);

        inlinePrinterHelper Helper(prefix, targetVar, callerParams, Policy);
        callee->getBody()->printPretty(SS, &Helper, Policy);
        SS.flush();

        size_t firstBrace = bodyStr.find('{');
        size_t lastBrace  = bodyStr.rfind('}');

        if (firstBrace != string::npos && lastBrace != string::npos && firstBrace < lastBrace)
            bodyStr = bodyStr.substr(firstBrace + 1, lastBrace - firstBrace - 1);

        size_t firstContent = bodyStr.find_first_not_of(" \n\r\t");
        size_t lastContent  = bodyStr.find_last_not_of(" \n\r\t");

        if (firstContent != string::npos && lastContent != string::npos)
            bodyStr = bodyStr.substr(firstContent, lastContent - firstContent + 1);

        addedLines += std::count(bodyStr.begin(), bodyStr.end(), '\n');
        OS << bodyStr;

        auto orig_text = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(stmtToReplace->getSourceRange()), SM, LangOpts);
        size_t net_lines = addedLines - std::count(orig_text.begin(), orig_text.end(), '\n');

        for(size_t i = currIndx + 1; i < (*callSite).size(); i++)
            if(((*callSite)[i].second == (*callSite)[currIndx].second) && 
                ((*callSite)[i].first > (*callSite)[currIndx].first)) (*callSite)[i].first += net_lines;

        OS.flush();
        rewriter->ReplaceText(stmtToReplace->getSourceRange(), inlinedCode);

        return true;
    }
};

void inlineCallback(ASTContext &AST, const std::string& caller, const std::string& callee, std::vector<std::pair<size_t, std::string>>* callSite, size_t currIndx, Rewriter* rewriter) {
    inlineVisitor *visitor = new inlineVisitor(&AST, caller, callee, callSite, currIndx, rewriter);
    visitor->TraverseDecl(AST.getTranslationUnitDecl());
}

void saveChanges(Rewriter* rewriter, const char* file) {
    SourceManager &SM = rewriter->getSourceMgr();
    FileID mainFileID = SM.getMainFileID();

    std::error_code EC;
    llvm::raw_fd_ostream outFile(file, EC);

    if(!EC) {
        const RewriteBuffer *RewriteBuf = rewriter->getRewriteBufferFor(mainFileID);
        if (RewriteBuf) {
            outFile << string(RewriteBuf->begin(), RewriteBuf->end());
            errs() << "[Invisible] Successfully emitted updated source to " << file << "\n";
        } else {
            llvm::StringRef originalContent = SM.getBufferData(mainFileID);
            outFile << originalContent;
            errs() << "[Invisible] Emitted original source to " << file << "\n";
        }
    } else {
        errs() << "[Invisible] unable to open output file when trying to save changes\n";
    }
}

enum class optPass {
    CLEANUP_FUNCTION_CALL_PASS,
    COMMA_INITIALIZATION_PASS,
    FILTER_PASS,
    INLINE_PASS,
    CONSTANT_FOLD_PASS
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

std::unique_ptr<FrontendActionFactory> getFrontendActionFactory(optPass optType, const std::string& caller, const std::string& callee, std::vector<std::pair<size_t, std::string>>* callSite, size_t currIndx, const char* file) {
    return std::make_unique<ToolActionFactory>([optType, caller, callee, callSite, currIndx, file](ASTContext &AST) {
        Rewriter rewriter;
        if(optType == optPass::INLINE_PASS) {
            inlineCallback(AST, caller, callee, callSite, currIndx, &rewriter);
            saveChanges(&rewriter, file);
        }
    });
}

std::unique_ptr<FrontendActionFactory> getFrontendActionFactory(optPass optType, const char* file = "") {
    return std::make_unique<ToolActionFactory>([optType, file](ASTContext &AST) {
        Rewriter rewriter;
        if (optType == optPass::COMMA_INITIALIZATION_PASS) {
            commaInitializationCallback(AST, &rewriter);
            saveChanges(&rewriter, file);
        } else if(optType == optPass::CLEANUP_FUNCTION_CALL_PASS) {
            cleanupFuncCallCallback(AST, &rewriter);
            saveChanges(&rewriter, file);
        } else if (optType == optPass::FILTER_PASS) {
            filterCallback(AST, &rewriter);
        } else if (optType == optPass::CONSTANT_FOLD_PASS) {
            constantFoldCallback(AST, &rewriter);
            saveChanges(&rewriter, file);
        }
    });
}

/**
 * Usage -
 * 
 * If using the preprocess functionality - ./invisible <path_to_base_directory> <file_path_relative_to_base_directory> <preprocess_functionality>
 * If using the inline functionality     - ./invisible <path_to_base_directory> <path_to_oracle_file> <pass_to_run> - for inline
 *                                       - ./invisible <path_to_base_directory> <file_path_relative_to_base_directory> <pass_to_run> - for constant fold
 */

 /**
  * TODO:
  *      - Handle inlining static variables. The problem with static variables is that they can't be hoisted at the top as they have to be initialized only once. 
  *        and they must be thread safe. Ofcourse replicating them at each inlined call is also out of the question. So for now, if we encounter a static variable 
  *        in the code, we just abort the inlining procedure. 
  *      - remove all this redundant disk I/O. Instead try and workout an in-process version of the code. 
  *      - take care of function pointer and array types in the comma-initialization pass [tick]
  *      - take care of function pointer and array indexing in the inlinePrinter module for the inlinePass
  *      - take care of determining whether or not the function is present at the top level to process it further. [tick]
  *      - come up with test cases for all the functionalities that the pass aim to perform. 
  *      - take account of printing the bool type as it is. Right now its printed as _Bool. [tick]
  */
int main(int argc, const char **argv) {
    if(argc == 4 && llvm::StringRef(argv[3]) == "only-preprocess") {
        int _argc = 2;
        std::string file = std::string(argv[1]) + std::string(argv[2]);
        const char* _argv[] = {"", file.c_str()};
        
        // preprocess step to convert comma initializers to stand-alone statements
        {
            cl::PrintOptionValues();
            auto ExpectedParser = CommonOptionsParser::create(_argc, _argv, cl::getGeneralCategory(), cl::ZeroOrMore, "optimization passes on the source code without tackling IR");
            if (!ExpectedParser) {
                llvm::errs() << ExpectedParser.takeError();
                return 1;
            }
            CommonOptionsParser &Options = ExpectedParser.get();
            ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());
            auto ActionFactory = getFrontendActionFactory(optPass::COMMA_INITIALIZATION_PASS, _argv[1]);
            int result = Tool.run(ActionFactory.get());
        }

        // preprocess step to remove any function calls as params in function calls
        do {
            auto ExpectedParser = CommonOptionsParser::create(_argc, _argv, cl::getGeneralCategory(), cl::ZeroOrMore, "optimization passes on the source code without tackling IR");
            if (!ExpectedParser) {
                llvm::errs() << ExpectedParser.takeError();
                return 1;
            }
            CommonOptionsParser &Options = ExpectedParser.get();
            ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());
            auto ActionFactory = getFrontendActionFactory(optPass::CLEANUP_FUNCTION_CALL_PASS, _argv[1]);
            int result = Tool.run(ActionFactory.get());
        } while(FuncCallCleaned);
        return 0;
    } else if(argc == 4 && llvm::StringRef(argv[3]) == "separate-functions") {
        int _argc = 2;
        std::string file = std::string(argv[1]) + std::string(argv[2]);
        const char* _argv[] = {"", file.c_str()};
        
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
    } else if(argc == 4 && llvm::StringRef(argv[3]) == "inline") {
        // oracle gives us the function to be inlined in topo sort for now
        std::vector<std::pair<std::string, std::string>> inline_oracle;
        std::vector<std::pair<size_t, std::string>> inline_debugLoc;
        std::ifstream infile(argv[2]);
    
        if (!infile.is_open()) {
            llvm::errs() << "Error: Could not open oracle file.\n";
        } else {
            std::string line;
            while (std::getline(infile, line)) {
                std::stringstream ss(line);
                std::string caller, callee, file;
                size_t row;
                if (ss >> caller >> callee >> row >> file) {
                    inline_oracle.emplace_back(caller, callee);
                    inline_debugLoc.emplace_back(row, file);
                }
            }
        }
    
        int _argc = 2;
        const char* _argv[2];
        _argv[0] = "";
    
        // iterate over the oracle and inline functions as required
        for (size_t i = 0; i < inline_oracle.size(); i++) {
            std::string file = std::string(argv[1]) + inline_debugLoc[i].second;
            _argv[1] = file.c_str();
            auto ExpectedParser = CommonOptionsParser::create(_argc, _argv, cl::getGeneralCategory(), cl::ZeroOrMore, "optimization passes on the source code without tackling IR");
            if (!ExpectedParser) {
                llvm::errs() << ExpectedParser.takeError();
                return 1;
            }
            CommonOptionsParser &Options = ExpectedParser.get();
            ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());
            auto ActionFactory = getFrontendActionFactory(optPass::INLINE_PASS, inline_oracle[i].first, inline_oracle[i].second, &inline_debugLoc, i, _argv[1]);
            int result = Tool.run(ActionFactory.get());
        }
    } else if(argc == 4 && llvm::StringRef(argv[3]) == "constant-fold") {
        int _argc = 2;
        std::string file = std::string(argv[1]) + std::string(argv[2]);
        const char* _argv[] = {"", file.c_str()};

        // preprocess step to remove any function calls as params in function calls
        do {
            auto ExpectedParser = CommonOptionsParser::create(_argc, _argv, cl::getGeneralCategory(), cl::ZeroOrMore, "optimization passes on the source code without tackling IR");
            if (!ExpectedParser) {
                llvm::errs() << ExpectedParser.takeError();
                return 1;
            }
            CommonOptionsParser &Options = ExpectedParser.get();
            ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());
            auto ActionFactory = getFrontendActionFactory(optPass::CONSTANT_FOLD_PASS, _argv[1]);
            int result = Tool.run(ActionFactory.get());
        } while(constantFolded);
        return 0;
    }
}

/*
export UV_CONCURRENT_DOWNLOADS=4
export UV_CONCURRENT_INSTALLS=4
export UV_INDEX_PRIVATE_REGISTRY_USERNAME=aws
export UV_INDEX_PRIVATE_REGISTRY_PASSWORD=$(aws codeartifact get-authorization-token --domain reveng --domain-owner 127214196211 \
  --query authorizationToken --output text --profile compiler-engineer-codeartifact --region eu-west-2)
uv pip install -e . --no-cache --reinstall
*/