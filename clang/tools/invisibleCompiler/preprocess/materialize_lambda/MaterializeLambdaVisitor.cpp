#include "preprocess/materialize_lambda/MaterializeLambdaVisitor.h"
#include "common/DeclBeforeDefPrinterHelper.h"
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

namespace {

/// The type aliases local to @p function that are used in what is traversed, and
/// the member types of @p record, whose scope the lambda is taken out of.
struct LocalAliasCollector : clang::RecursiveASTVisitor<LocalAliasCollector> {
    const clang::FunctionDecl* function;
    const clang::DeclContext* record;
    std::vector<const clang::TypedefNameDecl*> aliases;
    std::vector<const clang::NamedDecl*> members;
    bool VisitTypedefTypeLoc(clang::TypedefTypeLoc loc) {
        const clang::TypedefNameDecl* alias = loc.getDecl();
        if (alias->getDeclContext() == function && !llvm::is_contained(aliases, alias)) {
            aliases.push_back(alias);
            TraverseTypeLoc(alias->getTypeSourceInfo()->getTypeLoc());
        }
        addMember(alias);
        return true;
    }
    bool VisitTagTypeLoc(clang::TagTypeLoc loc) {
        addMember(loc.getDecl());
        return true;
    }
    void addMember(const clang::NamedDecl* decl) {
        if (record && decl->getDeclContext() == record && decl->getIdentifier() && !llvm::is_contained(members, decl))
            members.push_back(decl);
    }
};

/// Prints a lambda's body in its record: its implicit member accesses are the
/// ones of the `this` it captured.
class LambdaBodyPrinterHelper : public DeclBeforeDefPrinterHelper {
  public:
    LambdaBodyPrinterHelper(clang::ASTContext& ctx, clang::PrintingPolicy policy, std::string this_member)
        : DeclBeforeDefPrinterHelper(ctx, policy), m_ctx(ctx), m_this_member(std::move(this_member)) {}

    bool handledStmt(clang::Stmt* stmt, llvm::raw_ostream& stream) override {
        clang::SourceLocation member;
        if (auto* dependent = llvm::dyn_cast<clang::CXXDependentScopeMemberExpr>(stmt); dependent && dependent->isImplicitAccess())
            member = dependent->getMemberLoc();
        if (auto* unresolved = llvm::dyn_cast<clang::UnresolvedMemberExpr>(stmt); unresolved && unresolved->isImplicitAccess())
            member = unresolved->getMemberLoc();
        if (member.isValid() && !m_this_member.empty()) {
            stream << "this->" << m_this_member << "->"
                   << clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange({member, stmt->getEndLoc()}),
                                                  m_ctx.getSourceManager(), m_ctx.getLangOpts());
            return true;
        }
        return DeclBeforeDefPrinterHelper::handledStmt(stmt, stream);
    }

  private:
    clang::ASTContext& m_ctx;
    std::string m_this_member;
};

}  // namespace

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
    // the template heads of an out-of-line member of a class template
    for (unsigned i = 0; i < enclosing_func->getNumTemplateParameterLists(); ++i)
        param_lists.push_back(enclosing_func->getTemplateParameterList(i));
    if (auto* tmpl = enclosing_func->getDescribedFunctionTemplate())
        param_lists.push_back(tmpl->getTemplateParameters());

    // in a template, what `[&]` and `[=]` capture is only known to the instantiations:
    // the variables of the enclosing function that the body uses.
    bool dependent = call_op->isDependentContext();
    std::vector<const clang::VarDecl*> implicit_captures;
    if (lambda->getCaptureDefault() != clang::LCD_None && dependent) {
        std::function<void(const clang::Stmt*)> collect = [&](const clang::Stmt* stmt) {
            if (!stmt) return;
            if (const auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(stmt)) {
                const auto* var = llvm::dyn_cast<clang::VarDecl>(ref->getDecl());
                if (var && var->hasLocalStorage() && !call_op->Encloses(var->getDeclContext()) &&
                    !llvm::any_of(lambda->captures(),
                                  [var](const clang::LambdaCapture& capture) {
                                      return capture.capturesVariable() && capture.getCapturedVar() == var;
                                  }) &&
                    !llvm::is_contained(implicit_captures, var))
                    implicit_captures.push_back(var);
            }
            for (const clang::Stmt* child : stmt->children()) collect(child);
        };
        collect(call_op->getBody());
    }

    // a captured pack can't be a member, a record has a single pack parameter and a
    // conversion to a function pointer has no counterpart: such a lambda is left as it is.
    if (llvm::any_of(record->decls(), [](const clang::Decl* decl) {
            const auto* conversion = llvm::dyn_cast_or_null<clang::CXXConversionDecl>(decl->getAsFunction());
            return conversion && (decl->isUsed() || decl->isReferenced());
        }))
        return true;
    // in a template, the conversion is only used by the instantiations
    for (auto parents = m_ctx.getParents(*lambda); parents.size() == 1; parents = m_ctx.getParents(parents[0])) {
        if (const auto* var = parents[0].get<clang::VarDecl>()) {
            if (var->getType()->isFunctionPointerType()) return true;
            break;
        }
        const auto* expr = parents[0].get<clang::Expr>();
        if (!expr) break;
        if (const auto* binary = llvm::dyn_cast<clang::BinaryOperator>(expr)) {
            if (binary->isAssignmentOp() && binary->getLHS()->getType()->isFunctionPointerType()) return true;
            break;
        }
        if (const auto* unary = llvm::dyn_cast<clang::UnaryOperator>(expr)) {
            if (unary->getOpcode() == clang::UO_Plus) return true;
            break;
        }
        if (!llvm::isa<clang::ImplicitCastExpr, clang::ParenExpr, clang::MaterializeTemporaryExpr, clang::FullExpr,
                       clang::CXXBindTemporaryExpr>(expr))
            break;
    }
    auto is_pack = [](const clang::ValueDecl* var) { return var && var->isParameterPack(); };
    if (llvm::any_of(lambda->captures(),
                     [&](const clang::LambdaCapture& capture) {
                         return capture.isPackExpansion() || (capture.capturesVariable() && is_pack(capture.getCapturedVar()));
                     }) ||
        llvm::any_of(implicit_captures, is_pack))
        return true;
    std::vector<const clang::NamedDecl*> template_params;
    for (auto* list : param_lists) template_params.insert(template_params.end(), list->begin(), list->end());
    std::stable_partition(template_params.begin(), template_params.end(),
                          [](const clang::NamedDecl* param) { return !param->isParameterPack(); });
    if (llvm::count_if(template_params, [](const clang::NamedDecl* param) { return param->isParameterPack(); }) > 1)
        return true;

    // the members holding what is captured. In a template their types depend on it
    // and can't be spelled outside the enclosing function, they are template
    // parameters of the record given where the lambda was.
    struct Member {
        std::string name;
        std::string type;
    };
    std::vector<Member> members;
    auto type_of = [this](const clang::VarDecl* var, bool by_ref) {
        // a non-dependent `decltype(x)` isn't instantiated and keeps naming the x of the template
        clang::QualType type = var->getType();
        if (!type->isDependentType() && !type->isUndeducedType()) {
            if (!var->isInitCapture())
                type = by_ref ? (type->isReferenceType() ? type : m_ctx.getLValueReferenceType(type))
                              : type.getNonReferenceType();
            return type.getAsString(m_field_policy);
        }
        std::string init;
        if (var->isInitCapture()) {
            llvm::raw_string_ostream init_out(init);
            var->getInit()->printPretty(init_out, nullptr, m_policy);
        }
        if (by_ref) return "decltype((" + (var->isInitCapture() ? init : var->getName().str()) + "))&";
        return var->isInitCapture() ? "__decay(decltype((" + init + ")))"
                                    : "__remove_reference_t(decltype(" + var->getName().str() + "))";
    };
    auto capture_it = lambda->captures().begin();
    clang::FieldDecl* this_field = nullptr;
    for (clang::FieldDecl* field : record->fields()) {
        std::string field_name = "__parent_this__";
        std::string type = "decltype(this)";
        if (capture_it != lambda->captures().end()) {
            if (capture_it->capturesVariable()) {
                field_name = capture_it->getCapturedVar()->getName().str();
                type = type_of(llvm::cast<clang::VarDecl>(capture_it->getCapturedVar()),
                               capture_it->getCaptureKind() == clang::LCK_ByRef);
            } else if (capture_it->capturesThis()) {
                this_field = field;
                field->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(field_name)));
            }
            ++capture_it;
        }
        if (!dependent) {
            std::string printed;
            llvm::raw_string_ostream printed_out(printed);
            field->getType().print(printed_out, m_field_policy, field_name);
            type = printed;
        }
        members.push_back({field_name, type});
    }
    for (const clang::VarDecl* var : implicit_captures)
        members.push_back({var->getName().str(), type_of(var, lambda->getCaptureDefault() == clang::LCD_ByRef)});

    std::string struct_text;
    llvm::raw_string_ostream struct_out(struct_text);
    std::vector<std::string> params;
    if (dependent)
        for (size_t i = 0; i < members.size(); ++i) params.push_back("class __invisible_capture_" + std::to_string(i));
    for (const clang::NamedDecl* param : template_params)
        // as written, a parameter can be a pack or a non-type one
        params.push_back(clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(param->getSourceRange()),
                                                     m_ctx.getSourceManager(), m_ctx.getLangOpts())
                             .str());
    if (!params.empty()) struct_out << "template <" << llvm::join(params, ", ") << "> ";
    struct_out << "struct " << name << " {\n";
    // the type aliases of the enclosing function the lambda uses
    LocalAliasCollector local_aliases;
    local_aliases.function = enclosing_func;
    // out of the class of an out-of-line member, its member types are named through it
    local_aliases.record = enclosing_func->isOutOfLine() && enclosing_func->getQualifier() ? enclosing_func->getDeclContext() : nullptr;
    local_aliases.TraverseDecl(call_op);
    llvm::sort(local_aliases.aliases, [](const clang::TypedefNameDecl* left, const clang::TypedefNameDecl* right) {
        return left->getBeginLoc() < right->getBeginLoc();
    });
    for (const clang::TypedefNameDecl* alias : local_aliases.aliases)
        struct_out << clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(alias->getSourceRange()),
                                                  m_ctx.getSourceManager(), m_ctx.getLangOpts())
                   << ";\n";
    for (const clang::NamedDecl* member : local_aliases.members)
        struct_out << "using " << member->getName() << " = typename "
                   << clang::Lexer::getSourceText(
                          clang::CharSourceRange::getTokenRange(enclosing_func->getQualifierLoc().getSourceRange()),
                          m_ctx.getSourceManager(), m_ctx.getLangOpts())
                   << member->getName() << ";\n";
    for (size_t i = 0; i < members.size(); ++i) {
        if (dependent)
            struct_out << "__invisible_capture_" << i << " " << members[i].name << ";\n";
        else
            struct_out << members[i].type << ";\n";
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

    // `[]<class T>(T t)` has a call operator template, whose template parameters
    // aren't printed with it. The invented ones of `auto` parameters are left to them.
    if (!lambda->getExplicitTemplateParameters().empty()) {
        const clang::TemplateParameterList* params = lambda->getTemplateParameterList();
        auto text = [this](clang::SourceRange range) {
            return clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(range), m_ctx.getSourceManager(),
                                               m_ctx.getLangOpts());
        };
        struct_out << "template " << text({params->getLAngleLoc(), params->getRAngleLoc()});
        if (const clang::Expr* requires_clause = params->getRequiresClause())
            struct_out << " requires " << text(requires_clause->getSourceRange());
        struct_out << "\n";
    }
    // the body is printed with the helper, clang drops the `constexpr` of `if constexpr`
    clang::PrintingPolicy declaration_policy = m_policy;
    declaration_policy.TerseOutput = true;
    call_op->print(struct_out, declaration_policy);
    LambdaBodyPrinterHelper helper(m_ctx, m_policy, this_field ? "__parent_this__" : "");
    struct_out << " ";
    std::string body;
    llvm::raw_string_ostream body_out(body);
    call_op->getBody()->printPretty(body_out, &helper, m_policy);
    // a namespace alias of the enclosing function can't be a member, it opens the body
    std::string namespace_aliases;
    for (const clang::Decl* decl : enclosing_func->decls())
        if (llvm::isa<clang::NamespaceAliasDecl>(decl) &&
            m_ctx.getSourceManager().isBeforeInTranslationUnit(decl->getBeginLoc(), lambda->getBeginLoc()))
            namespace_aliases += "\n" +
                                 clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(decl->getSourceRange()),
                                                             m_ctx.getSourceManager(), m_ctx.getLangOpts())
                                     .str() +
                                 ";";
    body.insert(body.find('{') + 1, namespace_aliases);
    struct_out << body;
    struct_out << "\n};\n";
    struct_out.flush();

    // find a valid place to insert the recordDecl. Skipping past any
    // templates and attributes. 
    auto& sm = m_ctx.getSourceManager();
    clang::SourceLocation insert_loc = enclosing_func->getOuterLocStart();
    // before the template heads of the enclosing class of an out-of-line member too
    if (auto* tmpl = enclosing_func->getDescribedFunctionTemplate();
        tmpl && m_ctx.getSourceManager().isBeforeInTranslationUnit(tmpl->getBeginLoc(), insert_loc))
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
    std::vector<std::string> args;
    if (dependent)
        for (const Member& member : members) args.push_back(member.type);
    for (const clang::NamedDecl* param : template_params)
        args.push_back(param->getName().str() + (param->isParameterPack() ? "..." : ""));
    if (!args.empty()) ctor_out << "<" << llvm::join(args, ", ") << ">";
    ctor_out << "{";
    bool first = true;
    for (clang::Expr* init : lambda->capture_inits()) {
        if (!init) continue;
        if (!first) ctor_out << ", ";
        first = false;
        init->printPretty(ctor_out, nullptr, m_policy);
    }
    for (const clang::VarDecl* var : implicit_captures) {
        if (!first) ctor_out << ", ";
        first = false;
        ctor_out << var->getName();
    }
    ctor_out << "}";
    ctor_out.flush();

    m_rewriter.ReplaceText(lambda->getSourceRange(), ctor_text);
    m_changed = true;
    return true;
}

}  // namespace invisible
