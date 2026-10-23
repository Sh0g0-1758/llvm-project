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
#include "llvm/Support/xxhash.h"
#include "clang/AST/TemplateBase.h"

#include <string>
#include <queue>
#include <stack>

namespace invisible {

namespace {

// Collects the names a lambda uses that aren't visible before `function`, where its struct goes:
//  - the type aliases declared in `function`, and the aliases they use. The struct declares them
//    again, eg. `It` in
//
//        void f() { using It = int*; auto g = [](It p) { return *p; }; }
//
//  - the member types of `record`, the class of an out-of-line member function. The struct
//    declares them again through `X::`;
//  - whether it names a class or enum declared in the function. The struct can't name it at all.
struct LocalAliasCollector : clang::RecursiveASTVisitor<LocalAliasCollector> {
    const clang::FunctionDecl* function;
    const clang::DeclContext* record;
    const clang::DeclContext* lambda_call = nullptr;
    bool names_local_type = false;
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
        // a class or enum declared in the function, but not in the lambda, can't be named before
        // the function
        for (const clang::DeclContext* dc = loc.getDecl()->getDeclContext(); dc && dc != lambda_call; dc = dc->getParent())
            if (dc == function) names_local_type = true;
        return true;
    }
    void addMember(const clang::NamedDecl* decl) {
        if (record && decl->getDeclContext() == record && decl->getIdentifier() && !llvm::is_contained(members, decl))
            members.push_back(decl);
    }
};

// Prints a lambda's body as the body of its struct's call operator. In a template, clang leaves an
// implicit member access unresolved, eg. `size` in `[this] { return size; }`. It has no `this` in
// the AST to rewrite, so the helper prints it as `this->__parent_this__->size`. The other uses of
// `this` are rewritten in the AST, see replaceThis.
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

// --- MaterializeLambdaVisitor ----------------------------------------------

MaterializeLambdaVisitor::MaterializeLambdaVisitor(clang::ASTContext& ctx, clang::Rewriter& rewriter)
    : m_ctx(ctx), m_rewriter(rewriter), m_policy(codePolicy(ctx)), m_field_policy(codePolicy(ctx)) {
    m_policy.adjustForCPlusPlus();

    m_field_policy = m_policy;
    m_field_policy.PrintAsCanonical = true;
}

// whether a statement has a lambda in it, stopping at the first
class ContainsLambdaVisitor : public clang::RecursiveASTVisitor<ContainsLambdaVisitor> {
  public:
    bool found = false;
    bool VisitLambdaExpr(clang::LambdaExpr*) {
        found = true;
        return false;
    }
};

// a lambda with a lambda in its body waits until an iteration has turned the inner one into a struct
bool MaterializeLambdaVisitor::containsNestedLambda(clang::Stmt* node) {
    ContainsLambdaVisitor visitor;
    visitor.TraverseStmt(node);
    return visitor.found;
}

// The function the struct of @p lambda is put before: the nearest enclosing function that is
// neither a lambda nor a member of a local class, eg. `g` for the inner lambda of
// `void g() { [] { [] {}(); }(); }`. A struct can't go before those, as they are inside a function.
clang::FunctionDecl* MaterializeLambdaVisitor::enclosingFunction(const clang::LambdaExpr* lambda) {
    for (clang::DeclContext* dc = lambda->getLambdaClass()->getDeclContext(); dc; dc = dc->getParent()) {
        auto* function = llvm::dyn_cast<clang::FunctionDecl>(dc);
        if (!function) continue;
        auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
        if (method && (method->getParent()->isLambda() || isInsideFunction(method->getParent()))) continue;
        return function;
    }
    return nullptr;
}

// Whether @p lambda is used as a function pointer, eg. `+[] {}`. The struct can't be one, so such
// a lambda stays a lambda.
bool MaterializeLambdaVisitor::convertsToFunctionPointer(const clang::LambdaExpr* lambda) {
    if (llvm::any_of(lambda->getLambdaClass()->decls(), [](const clang::Decl* decl) {
            const auto* conversion = llvm::dyn_cast_or_null<clang::CXXConversionDecl>(decl->getAsFunction());
            return conversion && (decl->isUsed() || decl->isReferenced());
        }))
        return true;
    // in a template, clang only marks the conversion used in the instantiations, not in the
    // template's own lambda, so look at how the lambda is used: assigned to a function pointer, or `+`
    for (auto parents = m_ctx.getParents(*lambda); parents.size() == 1; parents = m_ctx.getParents(parents[0])) {
        if (const auto* var = parents[0].get<clang::VarDecl>()) return var->getType()->isFunctionPointerType();
        const auto* expr = parents[0].get<clang::Expr>();
        if (!expr) return false;
        if (const auto* binary = llvm::dyn_cast<clang::BinaryOperator>(expr))
            return binary->isAssignmentOp() && binary->getLHS()->getType()->isFunctionPointerType();
        if (const auto* unary = llvm::dyn_cast<clang::UnaryOperator>(expr)) return unary->getOpcode() == clang::UO_Plus;
        if (!llvm::isa<clang::ImplicitCastExpr, clang::ParenExpr, clang::MaterializeTemporaryExpr, clang::FullExpr,
                       clang::CXXBindTemporaryExpr>(expr))
            return false;
    }
    return false;
}

// Where the struct goes: before @p enclosing_func, its template heads, those of the class of an
// out-of-line member, and the attributes written before it, eg. before `[[nodiscard]]` in
// `[[nodiscard]] int f() { ... }`. Implicit attributes are skipped. eg. clang gives a function the
// visibility of `#pragma GCC visibility push` as an implicit attribute located at the pragma.
clang::SourceLocation MaterializeLambdaVisitor::structLoc(const clang::FunctionDecl* enclosing_func) {
    const clang::SourceManager& sm = m_ctx.getSourceManager();
    clang::SourceLocation loc = enclosing_func->getOuterLocStart();
    if (const auto* tmpl = enclosing_func->getDescribedFunctionTemplate();
        tmpl && sm.isBeforeInTranslationUnit(tmpl->getBeginLoc(), loc))
        loc = tmpl->getBeginLoc();
    for (const clang::Attr* attr : enclosing_func->attrs()) {
        if (attr->isImplicit() || attr->isInherited()) continue;
        clang::SourceLocation at = attr->getLocation();
        if (!at.isValid() || !sm.isBeforeInTranslationUnit(at, loc)) continue;
        // a standard attribute is located at its name, so step back over the `[[` before it
        while (auto prev = clang::Lexer::findPreviousToken(at, sm, m_ctx.getLangOpts(), false)) {
            if (!prev->is(clang::tok::l_square)) break;
            at = prev->getLocation();
        }
        loc = at;
    }
    return loc;
}

// Turns a lambda into a named struct, eg.
//
//     template <class F> int call(F f) { return f(1); }
//     int g(int n) { return call([&n](int x) { return x + n; }); }
//
// becomes
//
//     struct __invisible_lambda_1a2b3c4d5e_0 {
//     int &n;
//     int operator()(int x) const { return x + n; }
//     };
//     int g(int n) { return call(__invisible_lambda_1a2b3c4d5e_0{n}); }
//
// A lambda's type has no name, but decl-before-def has to name it when it writes out the
// specialization or copy of `call` for it, `call<__invisible_lambda_1a2b3c4d5e_0>`.
bool MaterializeLambdaVisitor::VisitLambdaExpr(clang::LambdaExpr* lambda) {
    clang::CXXMethodDecl* call_op = lambda->getCallOperator();
    if (containsNestedLambda(call_op->getBody()))
        return true;

    clang::FunctionDecl* enclosing_func = enclosingFunction(lambda);
    if (!enclosing_func || isSkippedByPass(enclosing_func)) return true;

    clang::CXXRecordDecl* record = lambda->getLambdaClass();

    // the type aliases of the enclosing function the lambda uses
    LocalAliasCollector local_aliases;
    local_aliases.function = enclosing_func;
    local_aliases.lambda_call = call_op;
    // the struct of an out-of-line member `X::f` is outside X, so the member types of X it uses
    // are declared again through `X::`
    local_aliases.record = enclosing_func->isOutOfLine() && enclosing_func->getQualifier() ? enclosing_func->getDeclContext() : nullptr;
    local_aliases.TraverseDecl(call_op);
    // a lambda stays a lambda if it names a class declared in the function, eg. as a parameter type,
    // or captures a value whose type can't be named. The struct goes before the function, where
    // neither type can be named.
    // FIXME: a lambda should also stay if it uses a variable of the function without capturing it,
    // eg. a static local, `static int count; [] { return ++count; }`, or a constant that isn't
    // odr-used, `const int N = 5; [=] { return N; }`. The struct uses it before it is declared.
    if (local_aliases.names_local_type ||
        llvm::any_of(record->fields(), [](const clang::FieldDecl* field) { return !isNameableAnywhere(field->getType()); }))
        return true;

    // The struct's name: a hash of the enclosing function's passKey, then the lambda's index in that
    // function, eg. `__invisible_lambda_1a2b3c4d5e_0`. Different functions get different names, and
    // an inline function gets the same names in every TU. The index continues after the structs
    // that earlier iterations of the pass wrote.
    std::string prefix =
        "__invisible_lambda_" + llvm::utohexstr(llvm::xxh3_64bits(passKey(enclosing_func)) & 0xffffffffffULL, true) + "_";
    auto [next_index, inserted] = m_next_index.try_emplace(prefix, 0);
    if (inserted) {
        llvm::StringRef text = m_ctx.getSourceManager().getBufferData(m_ctx.getSourceManager().getMainFileID());
        for (size_t at = text.find(prefix); at != llvm::StringRef::npos; at = text.find(prefix, at + 1)) {
            unsigned index = 0;
            if (!text.drop_front(at + prefix.size()).take_while(llvm::isDigit).getAsInteger(10, index))
                next_index->second = std::max(next_index->second, index + 1);
        }
    }
    std::string name = prefix + std::to_string(next_index->second++);

    // its template heads: those of the enclosing function, whose parameters the struct may use
    std::vector<clang::TemplateParameterList*> param_lists;
    // the template heads of an out-of-line member of a class template
    for (unsigned i = 0; i < enclosing_func->getNumTemplateParameterLists(); ++i)
        param_lists.push_back(enclosing_func->getTemplateParameterList(i));
    if (auto* tmpl = enclosing_func->getDescribedFunctionTemplate())
        param_lists.push_back(tmpl->getTemplateParameters());

    // in a template, clang only lists what `[&]` and `[=]` capture in the instantiations, not in the
    // template's own lambda. So collect them here: the local variables from outside the lambda
    // that its body uses and doesn't capture explicitly.
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

    // the struct has no conversion to a function pointer, so a lambda used as one stays
    if (convertsToFunctionPointer(lambda)) return true;
    // a captured pack can't be a member, so such a lambda stays
    auto is_pack = [](const clang::ValueDecl* var) { return var && var->isParameterPack(); };
    if (llvm::any_of(lambda->captures(),
                     [&](const clang::LambdaCapture& capture) {
                         return capture.isPackExpansion() || (capture.capturesVariable() && is_pack(capture.getCapturedVar()));
                     }) ||
        llvm::any_of(implicit_captures, is_pack))
        return true;
    // the enclosing function's template parameters, packs last: in a class template, a pack must be
    // the last parameter. So a lambda that needs two packs stays.
    std::vector<const clang::NamedDecl*> template_params;
    for (auto* list : param_lists) template_params.insert(template_params.end(), list->begin(), list->end());
    std::stable_partition(template_params.begin(), template_params.end(),
                          [](const clang::NamedDecl* param) { return !param->isParameterPack(); });
    if (llvm::count_if(template_params, [](const clang::NamedDecl* param) { return param->isParameterPack(); }) > 1)
        return true;

    // a member for each capture. Outside a template, `type` is the member's whole declaration, as the
    // closure has it, eg. for `[n, &v, this]`
    //
    //     int n;   std::vector<int> &v;   X *__parent_this__;
    //
    // In a template, the member's type is a template parameter of the struct, and `type` is the
    // template argument written where the lambda was, see capture_type:
    //
    //     template <class __invisible_capture_0, class T>
    //     struct __invisible_lambda_1a2b3c4d5e_0 { __invisible_capture_0 t; ... };
    //     __invisible_lambda_1a2b3c4d5e_0<__remove_reference_t(decltype(t)), T>{t}
    struct Member {
        std::string name;
        std::string type;
    };
    std::vector<Member> members;

    // The template argument of a capture in a template, eg.
    //
    //     int n;   [n]                 =>  int
    //     T t;     [t]                 =>  __remove_reference_t(decltype(t))   the declared type, by copy
    //              [&t]                =>  decltype((t))&                      `(t)` is an lvalue, `T&`
    //              [y = t.size()]      =>  __decay(decltype((t.size())))       as `auto y = t.size()`
    //              [&y = t.get()]      =>  decltype((t.get()))&
    //
    // A type that doesn't depend on the template is written out, not as `decltype(n)`. clang doesn't
    // instantiate a decltype that doesn't depend on the template. So in the specialization `f<int>`,
    // it still refers to the template's `n`, which canonicalize-var-decl names differently.
    auto capture_type = [this](const clang::VarDecl* var, bool by_ref) -> std::string {
        clang::QualType type = var->getType();
        if (!type->isDependentType() && !type->isUndeducedType()) {
            // `int& r` captured by copy is an `int`, `int n` by reference an `int&`. An init
            // capture's variable already has the member's type
            if (!var->isInitCapture())
                type = by_ref ? (type->isReferenceType() ? type : m_ctx.getLValueReferenceType(type))
                              : type.getNonReferenceType();
            return type.getAsString(m_field_policy);
        }
        // the variable, or what an init capture is initialized with
        std::string expr = var->getName().str();
        if (var->isInitCapture()) {
            expr.clear();
            llvm::raw_string_ostream expr_out(expr);
            var->getInit()->printPretty(expr_out, nullptr, m_policy);
        }
        if (by_ref) return "decltype((" + expr + "))&";
        if (var->isInitCapture()) return "__decay(decltype((" + expr + ")))";
        return "__remove_reference_t(decltype(" + expr + "))";
    };

    // The closure's fields are its captures, in order. A captured `this` needs a member name, as
    // `this` is a keyword. It becomes `__parent_this__`, and the body's `this` is rewritten to use it
    // below.
    auto capture_it = lambda->captures().begin();
    clang::FieldDecl* this_field = nullptr;
    for (clang::FieldDecl* field : record->fields()) {
        const clang::LambdaCapture* capture = capture_it != lambda->captures().end() ? &*capture_it++ : nullptr;
        const auto* var = capture && capture->capturesVariable() ? llvm::cast<clang::VarDecl>(capture->getCapturedVar()) : nullptr;
        std::string field_name = var ? var->getName().str() : "__parent_this__";
        if (capture && capture->capturesThis()) {
            this_field = field;
            field->setDeclName(clang::DeclarationName(&m_ctx.Idents.get(field_name)));
        }
        if (dependent) {
            members.push_back({field_name, var ? capture_type(var, capture->getCaptureKind() == clang::LCK_ByRef) : "decltype(this)"});
            continue;
        }
        // the type is printed around the name, as in `int (*fp)(int)`
        std::string declaration;
        llvm::raw_string_ostream declaration_out(declaration);
        field->getType().print(declaration_out, m_field_policy, field_name);
        members.push_back({field_name, declaration});
    }
    // what `[=]` and `[&]` capture in a template, which the closure has no field for
    for (const clang::VarDecl* var : implicit_captures)
        members.push_back({var->getName().str(), capture_type(var, lambda->getCaptureDefault() == clang::LCD_ByRef)});

    // the struct: its template head, the aliases it needs, the members and the call operator
    std::string struct_text;
    llvm::raw_string_ostream struct_out(struct_text);
    std::vector<std::string> params;
    if (dependent)
        for (size_t i = 0; i < members.size(); ++i) params.push_back("class __invisible_capture_" + std::to_string(i));
    for (const clang::NamedDecl* param : template_params)
        // copied as written, which keeps the `...` of a pack and the type of a non-type parameter
        params.push_back(clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(param->getSourceRange()),
                                                     m_ctx.getSourceManager(), m_ctx.getLangOpts())
                             .str());
    if (!params.empty()) struct_out << "template <" << llvm::join(params, ", ") << "> ";
    struct_out << "struct " << name << " {\n";
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

    // `this` in the body becomes the captured one, `this->__parent_this__`
    if (this_field) {
        clang::QualType record_type = m_ctx.getTypeDeclType(clang::ElaboratedTypeKeyword::None, std::nullopt, record);
        clang::Expr* new_this = clang::CXXThisExpr::Create(m_ctx, clang::SourceLocation(), m_ctx.getPointerType(record_type), true);
        clang::Expr* replacement = clang::MemberExpr::Create(
            m_ctx, new_this, true, clang::SourceLocation(), clang::NestedNameSpecifierLoc(), clang::SourceLocation(),
            this_field, clang::DeclAccessPair::make(this_field, this_field->getAccess()),
            clang::DeclarationNameInfo(this_field->getDeclName(), clang::SourceLocation()), nullptr,
            this_field->getType(), clang::VK_LValue, clang::OK_Ordinary, clang::NOUR_None);
        replaceThis(call_op->getBody(), replacement);
    }

    // `[]<class T>(T t)` has a call operator template. Printing the call operator doesn't print its
    // template head, so the head is copied from the source. Parameters declared `auto` need no head,
    // as they are printed as `auto`.
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
    // the call operator is printed without its body. The body is printed separately with the helper,
    // as clang's printer drops the `constexpr` of `if constexpr`.
    clang::PrintingPolicy declaration_policy = m_policy;
    declaration_policy.TerseOutput = true;
    // clang marks this call operator constexpr even when it calls a non-constexpr function, eg.
    // puts. Before C++23 that doesn't compile once written out, so drop constexpr in that case.
    llvm::SmallVector<clang::PartialDiagnosticAt, 8> notes;
    if (!m_ctx.getLangOpts().CPlusPlus23 && call_op->isConstexpr() && !clang::Expr::isPotentialConstantExpr(call_op, notes))
        call_op->setConstexprKind(clang::ConstexprSpecKind::Unspecified);
    call_op->print(struct_out, declaration_policy);
    LambdaBodyPrinterHelper helper(m_ctx, m_policy, this_field ? "__parent_this__" : "");
    struct_out << " ";
    std::string body;
    llvm::raw_string_ostream body_out(body);
    call_op->getBody()->printPretty(body_out, &helper, m_policy);
    // a namespace alias can't be a class member. So the namespace aliases the enclosing function
    // declares before the lambda are copied to the start of the call operator's body.
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

    // The struct of a static function goes in an unnamed namespace. Another TU may have a static
    // function of the same name, and its structs would get the same names.
    if (!enclosing_func->isExternallyVisible() && enclosing_func->getLexicalDeclContext()->isFileContext())
        struct_text = "namespace {\n" + struct_text + "}\n";
    m_rewriter.InsertTextBefore(structLoc(enclosing_func), struct_text);

    // the lambda becomes `__invisible_lambda_<hash>_N<template args>{captures...}`
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
