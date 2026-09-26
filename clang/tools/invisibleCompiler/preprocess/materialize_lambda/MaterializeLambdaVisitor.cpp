#include "preprocess/materialize_lambda/MaterializeLambdaVisitor.h"
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

// ─── MaterializeLambdaVisitor ──────────────────────────────────────────────

size_t MaterializeLambdaVisitor::s_counter = 0;

MaterializeLambdaVisitor::MaterializeLambdaVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(ctx.getLangOpts()), m_field_policy(ctx.getLangOpts()) {
    m_policy.adjustForCPlusPlus();

    m_field_policy = m_policy;
    m_field_policy.PrintAsCanonical = true;
}

class ContainsLambdaVisitor : public clang::RecursiveASTVisitor<ContainsLambdaVisitor> {
  public:
    bool found = false;
    bool VisitLambdaExpr(clang::LambdaExpr*) {
        found = true;
        return false;
    }
};

bool MaterializeLambdaVisitor::containsNestedLambda(clang::Stmt* node) {
    ContainsLambdaVisitor visitor;
    visitor.TraverseStmt(node);
    return visitor.found;
}

class ReplaceThisVisitor : public clang::RecursiveASTVisitor<ReplaceThisVisitor> {
  public:
    ReplaceThisVisitor(clang::Expr* replacement) : m_replacement(replacement) {}

    // skip traversing records as their methods will have CXXThisExpr
    // which we do not intend to modify. We only want to mutate top-level
    // presence of the `this` keyword.
    bool TraverseCXXRecordDecl(clang::CXXRecordDecl*) { return true; }

    bool TraverseStmt(clang::Stmt* stmt) {
        if (!stmt) return true;
        if (!clang::RecursiveASTVisitor<ReplaceThisVisitor>::TraverseStmt(stmt)) return false;
        replaceThisInChildren(stmt);
        // parents point at the semantic form of an InitListExpr, but RAV traverses
        // (and StmtPrinter prints) its syntactic form, which has its own array.
        if (auto* ile = llvm::dyn_cast<clang::InitListExpr>(stmt))
            replaceThisInChildren(ile->getSyntacticForm());
        return true;
    }

  private:
    void replaceThisInChildren(clang::Stmt* stmt) {
        if (!stmt) return;
        for (clang::Stmt*& child : stmt->children()) {
            if (llvm::isa_and_nonnull<clang::CXXThisExpr>(child))
                child = m_replacement;
        }
    }

    clang::Expr* m_replacement;
};

void MaterializeLambdaVisitor::rewriteCapturedThis(clang::Stmt* node, clang::Expr* replacement) {
    ReplaceThisVisitor visitor(replacement);
    visitor.TraverseStmt(node);
}

bool MaterializeLambdaVisitor::VisitLambdaExpr(clang::LambdaExpr* lambda) {
    /**
     * Lamda expressions are un-named record declarations. This becomes a problem
     * when we have to print their types, consider sending a lamda as an argument 
     * to a templated function.
     * 
     * Information is taken from the recordDecl stored as part of the lamdaExpr node
     * in the AST. The captured variables become member variables in this new record.
     * The function decl that the lamda represents become an operator() function. Captured
     * this is more nuanced since `this` is a keyword. We rename it, store it as a member
     * variable and mutate the AST in place to change thisExpr node to instead use this
     * member variable. The lamda expression is then replaced with a constructor call to 
     * this recordDecl along with the captured variables as initializers to this call. 
     * 
     * The recordDecl also respects the dependent nature (if any) of the enclosing function. 
     * This record is emitted just before the parent function, hence if a parent of this function
     * is templated, the recordDecl can access its template parameters. Hence, the recordDecl needs to
     * be templated only if the parent function of the lamda is templated. 
     * 
     * In case of nested lamdas, we traverse the lamdas bottom up. Each lamda is processed in isolation
     * ie. a lamda will never have any nested lamdas. 
     **/
    clang::CXXMethodDecl* call_op = lambda->getCallOperator();
    if (containsNestedLambda(call_op->getBody()))
        return true;

    clang::FunctionDecl* enclosing_func = nullptr;
    for (clang::DeclContext* dc = lambda->getLambdaClass()->getDeclContext(); dc; dc = dc->getParent()) {
        auto* fd = llvm::dyn_cast<clang::FunctionDecl>(dc);
        if (!fd) continue;
        auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(fd);
        if (method && (method->getParent()->isLambda() || isInsideFunction(method->getParent()))) continue;
        enclosing_func = fd;
        break;
    }
    if (!enclosing_func) return true;

    clang::CXXRecordDecl* record = lambda->getLambdaClass();

    std::string name = "__invisible_lambda_" + std::to_string(s_counter++);

    std::vector<clang::TemplateParameterList*> param_lists;
    if (auto* tmpl = enclosing_func->getDescribedFunctionTemplate())
        param_lists.push_back(tmpl->getTemplateParameters());

    std::string struct_text;
    llvm::raw_string_ostream struct_out(struct_text);
    if (!param_lists.empty()) {
        struct_out << "template <";
        bool first_param = true;
        for (auto* params : param_lists) {
            for (unsigned i = 0; i < params->size(); ++i) {
                if (!first_param) struct_out << ", ";
                first_param = false;
                struct_out << "typename " << params->getParam(i)->getName();
            }
        }
        struct_out << "> ";
    }
    struct_out << "struct " << name << " {\n";
    auto capture_it = lambda->captures().begin();
    clang::FieldDecl* this_field = nullptr;
    for (clang::FieldDecl* field : record->fields()) {
        std::string field_name = "__parent_this__";
        if (capture_it != lambda->captures().end()) {
            if (capture_it->capturesVariable())
                field_name = capture_it->getCapturedVar()->getName().str();
            else if (capture_it->capturesThis()) {
                this_field = field;
                field->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(field_name)));
            }
            ++capture_it;
        }
        field->getType().print(struct_out, m_field_policy, field_name);
        struct_out << ";\n";
    }

    if (this_field) {
        clang::QualType record_type = m_ctx.getTypeDeclType(clang::ElaboratedTypeKeyword::None, std::nullopt, record);
        clang::Expr* new_this = clang::CXXThisExpr::Create(m_ctx, clang::SourceLocation(), m_ctx.getPointerType(record_type), true);
        clang::Expr* replacement = clang::MemberExpr::Create(
            m_ctx, new_this, true, clang::SourceLocation(), clang::NestedNameSpecifierLoc(), clang::SourceLocation(),
            this_field, clang::DeclAccessPair::make(this_field, this_field->getAccess()),
            clang::DeclarationNameInfo(this_field->getDeclName(), clang::SourceLocation()), nullptr,
            this_field->getType(), clang::VK_LValue, clang::OK_Ordinary, clang::NOUR_None);
        rewriteCapturedThis(call_op->getBody(), replacement);
    }

    call_op->print(struct_out, m_policy);
    struct_out << "\n};\n";
    struct_out.flush();

    // find a valid place to insert the recordDecl. Skipping past any
    // templates and attributes. 
    auto& sm = m_ctx.getSourceManager();
    clang::SourceLocation insert_loc = enclosing_func->getBeginLoc();
    if (auto* tmpl = enclosing_func->getDescribedFunctionTemplate())
        insert_loc = tmpl->getBeginLoc();
    for (const clang::Attr* attr : enclosing_func->attrs()) {
        clang::SourceLocation loc = attr->getLocation();
        if (!loc.isValid() || !sm.isBeforeInTranslationUnit(loc, insert_loc)) continue;
        while (auto prev = clang::Lexer::findPreviousToken(loc, sm, m_ctx.getLangOpts(), false)) {
            if (!prev->is(clang::tok::l_square)) break;
            loc = prev->getLocation();
        }
        insert_loc = loc;
    }
    m_rewriter.InsertTextBefore(insert_loc, struct_text);

    std::string ctor_text;
    llvm::raw_string_ostream ctor_out(ctor_text);
    ctor_out << name;
    if (!param_lists.empty()) {
        ctor_out << "<";
        bool first_arg = true;
        for (auto* params : param_lists) {
            for (unsigned i = 0; i < params->size(); ++i) {
                if (!first_arg) ctor_out << ", ";
                first_arg = false;
                ctor_out << params->getParam(i)->getName();
            }
        }
        ctor_out << ">";
    }
    ctor_out << "{";
    bool first = true;
    for (clang::Expr* init : lambda->capture_inits()) {
        if (!init) continue;
        if (!first) ctor_out << ", ";
        first = false;
        init->printPretty(ctor_out, nullptr, m_policy);
    }
    ctor_out << "}";
    ctor_out.flush();

    m_rewriter.ReplaceText(lambda->getSourceRange(), ctor_text);
    m_changed = true;
    return true;
}

}  // namespace invisible
