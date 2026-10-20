// The preprocess pipeline. Each pass parses the previous pass's output and rewrites it, and every
// output is checked to compile. When one doesn't, the functions the errors are in are left as
// written and the pass runs again, see Pipeline. Also the entry points of the info and optimize
// modes. See docs/DESIGN.md sections 2, 4 and 5.

#include "invisible.h"
#include "helpers.h"

#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Core/Replacement.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Regex.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <tuple>

namespace invisible {

// ─── preprocess ────────────────────────────────────────────────────

namespace {

/// What a pass does with the AST of its input: edit the text through the rewriter.
using Visit = std::function<void(clang::ASTContext&, clang::Sema&, clang::Rewriter&)>;

// clang's printer writes `__typeof(x)` as `typeof(x)` and `__asm__("label")` as `asm("label")`,
// which only C23, C++ and the GNU modes accept, eg. glibc's
// `extern FILE *fopen(...) __asm__("fopen64");` comes back as `asm("fopen64")`, which `-std=c11`
// rejects. So every pass's output is respelled with `__typeof__` and `__asm__`, which every mode
// accepts. `asm` is only respelled where it starts an asm label or statement, as in C it may name
// a variable.
std::string spellGnuKeywords(const std::string& source) {
    clang::LangOptions lang_opts;
    lang_opts.CPlusPlus = true;
    clang::Lexer lexer(clang::SourceLocation(), lang_opts, source.data(), source.data(), source.data() + source.size());
    std::string result;
    size_t copied = 0;
    clang::Token token, next;
    lexer.LexFromRawLexer(token);
    while (token.isNot(clang::tok::eof)) {
        lexer.LexFromRawLexer(next);
        if (token.is(clang::tok::raw_identifier)) {
            llvm::StringRef name = token.getRawIdentifier();
            bool asm_start = name == "asm" && (next.is(clang::tok::l_paren) || (next.is(clang::tok::raw_identifier) &&
                                                                                   llvm::is_contained({"volatile", "goto", "inline"}, next.getRawIdentifier())));
            if (name == "typeof" || name == "typeof_unqual" || asm_start) {
                size_t at = name.data() - source.data();
                result.append(source, copied, at - copied).append("__" + name.str() + "__");
                copied = at + name.size();
            }
        }
        token = next;
    }
    return result.append(source, copied, std::string::npos);
}

/// An explicit instantiation declaration, eg. `extern template class basic_string<char>;`, as
/// the offsets of its text in the source.
struct ExternTemplate {
    size_t begin, end;
};

/// The explicit instantiation declarations of @p source, found with the raw lexer: `extern template`
/// up to the `;` that ends it, outside brackets.
std::vector<ExternTemplate> findExternTemplates(const std::string& source) {
    clang::LangOptions lang_opts;
    lang_opts.CPlusPlus = true;
    clang::Lexer lexer(clang::SourceLocation(), lang_opts, source.data(), source.data(), source.data() + source.size());
    std::vector<ExternTemplate> found;
    clang::Token token, next;
    lexer.LexFromRawLexer(token);
    while (token.isNot(clang::tok::eof)) {
        lexer.LexFromRawLexer(next);
        if (token.is(clang::tok::raw_identifier) && token.getRawIdentifier() == "extern" && next.is(clang::tok::raw_identifier) &&
            next.getRawIdentifier() == "template") {
            size_t begin = token.getRawIdentifier().data() - source.data();
            int depth = 0;
            while (next.isNot(clang::tok::eof) && !(depth == 0 && next.is(clang::tok::semi))) {
                if (next.isOneOf(clang::tok::l_paren, clang::tok::l_brace, clang::tok::l_square)) ++depth;
                if (next.isOneOf(clang::tok::r_paren, clang::tok::r_brace, clang::tok::r_square)) --depth;
                lexer.LexFromRawLexer(next);
            }
            if (next.is(clang::tok::eof)) break;
            found.push_back({begin, lexer.getCurrentBufferOffset()});
            lexer.LexFromRawLexer(next);
        }
        token = next;
    }
    return found;
}

/// @p source without the explicit instantiation declarations @p decls, but those of @p kept.
std::string dropExternTemplates(const std::string& source, const std::vector<ExternTemplate>& decls, const std::set<size_t>& kept) {
    std::string result;
    size_t copied = 0;
    for (size_t i = 0; i < decls.size(); ++i) {
        if (kept.count(i)) continue;
        result.append(source, copied, decls[i].begin - copied);
        copied = decls[i].end;
    }
    return result.append(source, copied, std::string::npos);
}

/// The identifiers of @p text, eg. `basic_string`, `char` and `allocator` of
/// `extern template class basic_string<char, allocator<char>>;`, numbers left out.
std::vector<llvm::StringRef> identifiers(llvm::StringRef text) {
    std::vector<llvm::StringRef> words;
    auto is_word = [](char c) { return llvm::isAlnum(c) || c == '_'; };
    for (size_t at = 0; at < text.size();) {
        size_t end = at;
        while (end < text.size() && is_word(text[end])) ++end;
        if (end > at && !llvm::isDigit(text[at])) words.push_back(text.slice(at, end));
        at = std::max(end, at + 1);
    }
    return words;
}

/// The names of the functions and classes whose instantiation the notes of @p diagnostics report,
/// eg. `__ostream_insert` and `basic_string` of `std::basic_string<char>::append`.
std::set<std::string> instantiatedNames(const std::string& diagnostics) {
    static const llvm::Regex instantiation("note: in instantiation of [^']*'([^']+)' requested here");
    std::set<std::string> names;
    llvm::SmallVector<llvm::StringRef, 16> lines;
    llvm::StringRef(diagnostics).split(lines, '\n');
    for (llvm::StringRef line : lines) {
        llvm::SmallVector<llvm::StringRef, 2> match;
        if (!instantiation.match(line, &match)) continue;
        // the identifiers of the name outside its template arguments
        std::string outside;
        int depth = 0;
        for (char c : match[1]) {
            if (c == '<' || c == '(') ++depth;
            else if (c == '>' || c == ')') --depth;
            else if (depth == 0) outside += c;
        }
        for (llvm::StringRef word : identifiers(outside)) names.insert(word.str());
    }
    return names;
}

/// Drops the explicit instantiation declarations of @p source, eg. libstdc++'s
/// `extern template class basic_string<char>;`, so that what they name is instantiated in the TU.
///
/// The problem: such a declaration says the instantiation is in another TU, eg. libstdc++.so, and
/// clang then gives the TU no instantiation of its own. With
///
///     template <class T> inline T twice(T x) { return x + x; }
///     extern template int twice<int>(int);
///     int use(int a) { return twice(a); }
///
///  - an inline function, as `twice` here or a member defined in its class, is still instantiated,
///    but emitted `available_externally`: a body for the optimizer only, the code being the other
///    TU's. LLVM inlines `twice<int>` into `use`, and calls into such bodies, eg. into a constructor
///    of `basic_string<char>`, which decl-before-def can't write out, as a specialization after the
///    declaration is an error. The oracle then names sites in functions that have no text of their
///    own in the TU, only the template's, and the inliner finds no call there;
///  - a function that isn't inline isn't instantiated at all: without the `inline`, `twice<int>`
///    has no body, gets no copy, and its calls are never inlined.
/// Dropped, the declarations leave plain instantiations, which are copied and specialized like any
/// other, and on the dev units notably more oracle sites are inlined than with them kept.
///
/// Why it is right: the other TU's `twice<int>` is the template's body, as the TU's own instantiation
/// is. A body of its own would be an explicit specialization, `template <> int twice<int>(int x)
/// { return x * 3; }`, which every TU using `twice<int>` has to declare; a TU that doesn't is
/// ill-formed, no diagnostic required. It links, and differs at run time: with the specialization
/// in another TU only, `use` runs `x * 3` when `twice` isn't inline, the call reaching it, and
/// `x + x` when it is, LLVM having inlined the template's body (see experiments/extern_template_odr).
/// So in a valid program an instantiation made here is the one the binary uses, and LLVM relies on
/// the same when it inlines an `available_externally` body.
///
/// Some can't be dropped: without `extern template ostream &__ostream_insert(ostream &, const char *,
/// streamsize);` the TU instantiates `__ostream_insert`, which fails when basic_ostream is only
/// declared. So all are dropped, and while that doesn't compile, those naming what the errors'
/// instantiation notes name are put back. If it never compiles, the source is returned as it was.
std::string dropInstantiableExternTemplates(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::vector<ExternTemplate> decls = findExternTemplates(source);
    std::set<size_t> kept;
    // drop them all, then keep back those the errors name, until it compiles
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        std::string dropped = dropExternTemplates(source, decls, kept);
        if (decls.size() == kept.size() || isCompilable(dropped, filename, args)) return dropped;
        size_t known = kept.size();
        std::set<std::string> names = instantiatedNames(lastCompileFailure().diagnostics);
        for (size_t i = 0; i < decls.size(); ++i)
            if (llvm::any_of(identifiers(llvm::StringRef(source).slice(decls[i].begin, decls[i].end)),
                             [&](llvm::StringRef word) { return names.count(word.str()); }))
                kept.insert(i);
        if (kept.size() == known) break;
    }
    // keeping every one is what the input was, which compiles
    return source;
}

/// Runs @p visit on the AST of @p source and returns the rewritten source, its GNU keywords
/// respelled. Every pass but insert-braces is a call of it.
std::string rewrite(const std::string& source, const std::string& filename, std::vector<std::string>& args, const Visit& visit) {
    clang::Rewriter rewriter;
    return spellGnuKeywords(runInMemory(source, filename, args, [&rewriter, &visit](clang::ASTContext& ctx, clang::Sema& sema) {
        rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        visit(ctx, sema, rewriter);
    }, rewriter));
}

// Inserts braces around single-statement control flow bodies, with clang-format's InsertBraces, eg.
// `if (c) f();` becomes `if (c) { f(); }`, so that a later pass always has a block to put a
// hoisted call's temporary in.
std::string insertBraces(const std::string& source) {
    static const auto style = []() {
        auto s = clang::format::getLLVMStyle();
        s.InsertBraces = true;
        s.ColumnLimit = 0;
        return s;
    }();
    auto replacements = clang::format::reformat(style, source, {clang::tooling::Range(0, source.size())});
    auto formatted = clang::tooling::applyAllReplacements(source, replacements);
    return formatted ? std::move(*formatted) : source;
}

/// Adds to @p kept the functions named by @p names (passKeys), or their templates, so that
/// decl-before-def leaves them as written along with what KeepInPlaceVisitor found, eg. what a
/// later pass needs, see laterUnmoved.
void keepByName(clang::ASTContext& ctx, const std::set<std::string>& names, std::set<const clang::FunctionDecl*>& kept) {
    if (names.empty()) return;
    struct Finder : clang::RecursiveASTVisitor<Finder> {
        const std::set<std::string>& names;
        std::set<const clang::FunctionDecl*>& kept;
        Finder(const std::set<std::string>& names, std::set<const clang::FunctionDecl*>& kept) : names(names), kept(kept) {}
        bool shouldVisitTemplateInstantiations() const { return true; }
        bool VisitFunctionDecl(clang::FunctionDecl* func) {
            if (llvm::isa<clang::CXXDeductionGuideDecl>(func) || !names.count(passKey(func))) return true;
            const clang::FunctionDecl* pattern = func->getTemplateInstantiationPattern();
            kept.insert((pattern ? pattern : func)->getCanonicalDecl());
            return true;
        }
    } finder(names, kept);
    finder.TraverseDecl(ctx.getTranslationUnitDecl());
}

/// The functions the errors of @p diagnostics are in, by passKey, which the per-function fallback
/// leaves as written: for each `a.cc:812:5: error:`, the smallest definition of @p source around
/// line 812, eg. the member of a local class rather than the function it is in. A lambda was made
/// a struct before the function it was in, so an error in the struct is given to the next
/// definition after it, that function.
std::vector<std::string> functionsAround(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                                         const std::string& diagnostics) {
    static const llvm::Regex error("^[^ ]*:([0-9]+):[0-9]+: error:");
    std::vector<unsigned> lines;
    llvm::SmallVector<llvm::StringRef, 16> diagnostic_lines;
    llvm::StringRef(diagnostics).split(diagnostic_lines, '\n');
    for (llvm::StringRef diagnostic : diagnostic_lines) {
        llvm::SmallVector<llvm::StringRef, 2> match;
        unsigned line = 0;
        if (error.match(diagnostic, &match) && !match[1].getAsInteger(10, line)) lines.push_back(line);
    }
    std::vector<std::string> names;
    clang::Rewriter rewriter;
    // the lines of every function definition, then for each error the smallest around it
    runInMemory(source, filename, args, [&](clang::ASTContext& ctx, clang::Sema&) {
        rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        struct Definitions : clang::RecursiveASTVisitor<Definitions> {
            clang::SourceManager& sm;
            std::vector<std::tuple<unsigned, unsigned, const clang::FunctionDecl*>> found;
            explicit Definitions(clang::SourceManager& sm) : sm(sm) {}
            bool VisitFunctionDecl(clang::FunctionDecl* func) {
                if (!func->doesThisDeclarationHaveABody() || llvm::isa<clang::CXXDeductionGuideDecl>(func))
                    return true;
                const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(func);
                if (method && method->getParent()->getName().starts_with("__invisible_lambda_")) return true;
                found.push_back({sm.getPresumedLineNumber(func->getBeginLoc()), sm.getPresumedLineNumber(func->getEndLoc()), func});
                return true;
            }
        } definitions(ctx.getSourceManager());
        definitions.TraverseDecl(ctx.getTranslationUnitDecl());
        for (unsigned line : lines) {
            const clang::FunctionDecl* best = nullptr;
            unsigned best_span = ~0u, next_begin = ~0u;
            const clang::FunctionDecl* next = nullptr;
            for (const auto& [begin, end, func] : definitions.found) {
                if (begin <= line && line <= end && end - begin < best_span) {
                    best = func;
                    best_span = end - begin;
                }
                if (begin > line && begin < next_begin) {
                    next = func;
                    next_begin = begin;
                }
            }
            if (!best) best = next;
            if (best) names.push_back(passKey(best));
        }
    }, rewriter);
    return names;
}

/// What decl-before-def must leave as written because a later pass needs it, by passKey, eg. a
/// template it reduced to a declaration whose instantiation hoist-calls writes, see
/// neededAsWritten. Learned when a later pass fails, and kept through the reruns of the TU.
std::set<std::string>& laterUnmoved() {
    static std::set<std::string> keys;
    return keys;
}

/// The templates decl-before-def reduced to declarations, by passKey: a template whose
/// instantiations all got copies keeps only its declaration, eg. `template <class T> T twice(T);`.
/// Kept for the TU, as a later pass can still need one.
std::set<std::string>& strippedTemplates() {
    static std::set<std::string> keys;
    return keys;
}

/// The templates DeclBeforeDef reduced to declarations, by passKey, declared on @p lines of @p source,
/// eg. the lines a `note: declared here` points at.
std::set<std::string> strippedTemplatesDeclaredAt(const std::string& source, const std::string& filename,
                                                  std::vector<std::string>& args, const std::set<unsigned>& lines) {
    std::set<std::string> keys;
    if (lines.empty()) return keys;
    clang::Rewriter rewriter;
    runInMemory(source, filename, args, [&](clang::ASTContext& ctx, clang::Sema&) {
        rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        struct Declarations : clang::RecursiveASTVisitor<Declarations> {
            clang::SourceManager& sm;
            const std::set<unsigned>& lines;
            std::set<std::string>& keys;
            Declarations(clang::SourceManager& sm, const std::set<unsigned>& lines, std::set<std::string>& keys)
                : sm(sm), lines(lines), keys(keys) {}
            bool VisitFunctionDecl(clang::FunctionDecl* func) {
                if (func->isDependentContext() && lines.count(sm.getPresumedLineNumber(func->getLocation())) &&
                    strippedTemplates().count(passKey(func)))
                    keys.insert(passKey(func));
                return true;
            }
        } declarations(ctx.getSourceManager(), lines, keys);
        declarations.TraverseDecl(ctx.getTranslationUnitDecl());
    }, rewriter);
    return keys;
}

/// The templates DeclBeforeDef reduced to declarations that @p source uses an instantiation of,
/// by passKey: the instantiation is undefined, eg. a conversion to bool that hoist-calls writes
/// for the variable of a condition, `bool b(ptr);`, where the conversion called before was copied.
std::set<std::string> strippedTemplatesUsedIn(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::set<std::string> keys;
    if (strippedTemplates().empty()) return keys;
    clang::Rewriter rewriter;
    runInMemory(source, filename, args, [&](clang::ASTContext& ctx, clang::Sema&) {
        rewriter.setSourceMgr(ctx.getSourceManager(), ctx.getLangOpts());
        struct Instantiations : clang::RecursiveASTVisitor<Instantiations> {
            std::set<std::string>& keys;
            explicit Instantiations(std::set<std::string>& keys) : keys(keys) {}
            bool shouldVisitTemplateInstantiations() const { return true; }
            bool VisitFunctionDecl(clang::FunctionDecl* func) {
                const clang::FunctionDecl* pattern = func->getTemplateInstantiationPattern();
                if (pattern && func->isUsed() && !func->isDefined() && strippedTemplates().count(passKey(pattern)))
                    keys.insert(passKey(pattern));
                return true;
            }
        } instantiations(keys);
        instantiations.TraverseDecl(ctx.getTranslationUnitDecl());
    }, rewriter);
    return keys;
}

/// decl-before-def only sees the code as it was when it ran, and a later pass can write code that
/// needs what it took away. This finds two such errors in the @p diagnostics of @p source and
/// returns what decl-before-def must then leave as written, by passKey:
///  - a note pointing at a template it reduced to a declaration, eg. hoist-calls writes
///    `bool b(ptr);` for a condition, which instantiates the conversion to bool of `ptr`'s class,
///    whose template was reduced as the call decl-before-def saw went to a copy;
///  - "explicit specialization after instantiation" on a specialization it wrote, which the new
///    code instantiates before the specialization is declared.
std::set<std::string> neededAsWritten(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                                      const std::string& diagnostics) {
    static const llvm::Regex declared("^[^ ]*:([0-9]+):[0-9]+: note: .*(declared here|declaration of template entity is here)");
    static const llvm::Regex after_instantiation("^[^ ]*:([0-9]+):[0-9]+: error: explicit specialization of .* after instantiation");
    llvm::SmallVector<llvm::StringRef, 16> lines, source_lines;
    llvm::StringRef(diagnostics).split(lines, '\n');
    llvm::StringRef(source).split(source_lines, '\n');
    std::set<unsigned> declarations;
    std::string specializations;
    for (llvm::StringRef diagnostic : lines) {
        llvm::SmallVector<llvm::StringRef, 2> match;
        unsigned line = 0;
        if (declared.match(diagnostic, &match) && !match[1].getAsInteger(10, line)) declarations.insert(line);
        else if (after_instantiation.match(diagnostic, &match) && !match[1].getAsInteger(10, line) && line &&
                 line <= source_lines.size() && source_lines[line - 1].contains('{'))
            specializations += diagnostic.str() + "\n";
    }
    std::set<std::string> keys = strippedTemplatesDeclaredAt(source, filename, args, declarations);
    if (!specializations.empty())
        for (const std::string& name : functionsAround(source, filename, args, specializations))
            if (name.compare(0, 9, "template ") != 0) keys.insert(name);
    return keys;
}

/// What a run of DeclBeforeDef wrote, and what the retries of declBeforeDef learn from it: which
/// lines of the result each deferred function is on, which copy each `__mono` name stands for, which
/// declaration replaced which definition, and which templates were reduced to declarations.
struct DeclBeforeDefRun {
    std::string source;
    /// what it put at the end, and the lines of each function in it
    std::string deferred;
    std::vector<DeclBeforeDefVisitor::Entry> entries;
    std::map<std::string, std::string> copy_keys;
    std::vector<std::pair<std::string, std::string>> replaced, stripped;
};

/// Runs decl-before-def once, leaving the functions of @p unmoved as written and giving those of
/// @p dropped no copy. Its helper visitors change the AST, eg. rename the local variables, and the
/// changes only show in what decl-before-def prints from that same AST, so they all run in this one
/// parse: what stays in place is found first, then the AST is changed, then everything is printed.
DeclBeforeDefRun runDeclBeforeDef(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                                  const std::set<std::string>& unmoved, const std::set<std::string>& dropped) {
    DeclBeforeDefRun run;
    run.source = rewrite(source, filename, args, [&](clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter) {
        clang::TranslationUnitDecl* tu = ctx.getTranslationUnitDecl();
        // before the AST is changed below
        KeepInPlaceVisitor keep_in_place(ctx, unmoved, dropped);
        std::set<const clang::FunctionDecl*> kept_in_place = keep_in_place.run();
        keepAmbiguousOperators(ctx, sema, kept_in_place);
        keepByName(ctx, unmoved, kept_in_place);
        // the constructors moving and copying hoisted temporaries will call
        instantiateConstructorsOfValues(ctx, sema);

        // changes to the AST, which the printer then prints, see docs/DESIGN.md section 6.4
        FunctionalCastToCStyleCastVisitor(ctx, sema).TraverseDecl(tu);
        CanonicalizeVarDeclVisitor(ctx, rewriter).TraverseDecl(tu);
        BreakLocalRecordDeclStmtVisitor(ctx, rewriter).TraverseDecl(tu);
        MarkLocalRecordDeclMethodsExplicitVisitor(ctx, rewriter).TraverseDecl(tu);

        DeclBeforeDefVisitor visitor(ctx, sema, rewriter, kept_in_place, dropped, keep_in_place.specialized());
        visitor.TraverseDecl(tu);
        run.deferred = visitor.getDeferredDefinitions();
        run.entries = visitor.entries();
        run.copy_keys = visitor.copyKeys();
        run.replaced = visitor.replaced();
        run.stripped = visitor.stripped();
        for (const auto& [key, text] : run.stripped) strippedTemplates().insert(key);
        const clang::SourceManager& sm = ctx.getSourceManager();
        rewriter.InsertTextAfter(sm.getLocForEndOfFile(sm.getMainFileID()), run.deferred);
    });
    return run;
}

/// Blames the errors of @p diagnostics on what @p run wrote: a specialization, or a declaration
/// written in place of a definition, is left as written (@p unmoved), and a copy isn't given
/// (@p dropped). Returns whether either grew. eg. for
///
///     a.cc:4123:12: error: no member named 'x' in 'Box<int>'
///
/// when line 4123 is in the deferred specialization of `Box<int>::get`, that specialization is
/// left as written on the next attempt; when it is in the copy `get__mono3f2a...`, no copy is
/// given and its callers call `Box<int>::get` as written.
bool blameErrors(const DeclBeforeDefRun& run, const std::string& diagnostics, std::set<std::string>& unmoved,
                 std::set<std::string>& dropped) {
    bool changed = false;
    // the lines of what was put at the end start after the lines of the rest
    size_t start = std::count(run.source.begin(), run.source.end(), '\n') - std::count(run.deferred.begin(), run.deferred.end(), '\n');
    // an error, or the note of where it was instantiated from
    static const llvm::Regex error("^[^ ]*:([0-9]+):[0-9]+: (error:|note: (while substituting|in instantiation of))");
    static const llvm::Regex copy_name("([A-Za-z_][A-Za-z0-9_]*__mono[0-9a-f]+)");
    static const llvm::Regex declared("^[^ ]*:([0-9]+):[0-9]+: note: .*(declared here|declaration of template entity is here)");
    llvm::SmallVector<llvm::StringRef, 0> lines;
    llvm::StringRef(run.source).split(lines, '\n');
    llvm::SmallVector<llvm::StringRef, 2> match;
    llvm::SmallVector<llvm::StringRef, 16> messages;
    llvm::StringRef(diagnostics).split(messages, '\n');

    // two errors are blamed by their notes rather than by their lines: one that a note puts on a
    // template reduced to a declaration, eg. an instantiation without a definition, keeps the
    // template as written; and "cannot be used before it is defined" of a function written at the
    // end, eg. its deduced return type needed by a declaration before it, keeps that function in
    // place, as the note points at its declaration there
    std::vector<bool> handled(messages.size(), false);
    for (size_t begin = 0, end; begin < messages.size(); begin = end) {
        for (end = begin + 1; end < messages.size() && !messages[end].contains(": error: ") && !messages[end].contains(": warning: ");
             ++end) {}
        bool needs_definition = messages[begin].contains("cannot be used before it is defined");
        for (size_t i = begin; i < end; ++i) {
            size_t line = 0;
            if (!declared.match(messages[i], &match) || match[1].getAsInteger(10, line) || !line || line > lines.size()) continue;
            for (const auto& [key, text] : run.stripped)
                if (text.find('(') != std::string::npos && lines[line - 1].contains(text)) {
                    changed |= unmoved.insert(key).second;
                    std::fill(handled.begin() + begin, handled.begin() + end, true);
                }
            if (needs_definition && line >= start)
                for (const auto& entry : run.entries)
                    if (!entry.key.empty() && !entry.copy && entry.first_line <= line - start && line - start <= entry.last_line) {
                        changed |= unmoved.insert(entry.key).second;
                        std::fill(handled.begin() + begin, handled.begin() + end, true);
                    }
        }
    }
    // the other errors, by the line they are on
    for (size_t i = 0; i < messages.size(); ++i) {
        if (!error.match(messages[i], &match)) continue;
        size_t line = 0;
        match[1].getAsInteger(10, line);
        verbose() << "decl-before-def: " << messages[i] << " | "
                  << (line && line <= lines.size() ? lines[line - 1].trim().take_front(300) : "") << "\n";
        if (handled[i]) continue;
        if (line >= start) {
            for (const auto& entry : run.entries)
                if (!entry.key.empty() && entry.first_line <= line - start && line - start <= entry.last_line)
                    changed |= (entry.copy ? dropped : unmoved).insert(entry.key).second;
        } else if (line) {
            // a declaration written in place of a definition
            for (const auto& [key, text] : run.replaced)
                if (text.size() > 8 && lines[line - 1].contains(text)) changed |= unmoved.insert(key).second;
        }
        // a call of a copy that doesn't compile calls what it copies instead
        for (llvm::StringRef text = line && line <= lines.size() ? lines[line - 1] : ""; copy_name.match(text, &match);
             text = text.substr(text.find(match[1]) + match[1].size()))
            if (auto found = run.copy_keys.find(match[1].str()); found != run.copy_keys.end())
                changed |= dropped.insert(found->second).second;
    }
    return changed;
}

/// decl-before-def writes every deferred definition, specialization and copy of the TU, and some of
/// it may not compile, eg. a specialization the printer gets wrong. So it is retried: the errors
/// are blamed on what it wrote (blameErrors), which is left as written or not copied on the next
/// attempt, up to 20 more times, until it compiles or the errors blame nothing new. The last
/// attempt is returned either way, and the driver's check fails the pass if it still doesn't
/// compile. See docs/DESIGN.md section 4.2.
std::string declBeforeDef(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    std::set<std::string> dropped, unmoved = laterUnmoved();
    std::vector<std::string> check_args = args;
    check_args.push_back("-ferror-limit=0");
    for (unsigned attempt = 0;; ++attempt) {
        DeclBeforeDefRun run = runDeclBeforeDef(source, filename, args, unmoved, dropped);
        if (attempt == 20) return run.source;
        // an instantiation of a template reduced to a declaration is undefined, and clang only
        // reports the first of them
        std::set<std::string> undefined = strippedTemplatesUsedIn(run.source, filename, args);
        bool compiles = isCompilable(run.source, filename, check_args);
        if (compiles && undefined.empty()) return run.source;
        bool changed = false;
        for (const std::string& key : undefined) changed |= unmoved.insert(key).second;
        if (!compiles) changed |= blameErrors(run, lastCompileFailure().diagnostics, unmoved, dropped);
        verbose() << "decl-before-def: attempt " << attempt + 1 << " left " << unmoved.size() << " as written and "
                  << dropped.size() << " copies out\n";
        if (!changed) return run.source;
    }
}

/// A preprocess pass: its name and how to run one iteration of it.
struct Pass {
    PassInfo info;
    /// Runs one iteration on @p source and returns its output. A repeating pass
    /// sets @p changed to run again.
    std::function<std::string(const std::string& source, unsigned iteration, bool& changed)> run;
};

/// Runs @p Visitor, made of the AST and the rewriter, over the TU once, eg.
/// `visitOnce<CommaInitVisitor>(source, filename, args)` for a pass that is just a visitor.
template <class Visitor>
std::string visitOnce(const std::string& source, const std::string& filename, std::vector<std::string>& args) {
    return rewrite(source, filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
        Visitor(ctx, rewriter).TraverseDecl(ctx.getTranslationUnitDecl());
    });
}

/// The preprocess passes in order, see docs/DESIGN.md section 5. @p current_index numbers the
/// variables short-circuit-to-ir and hoist-calls name.
std::vector<Pass> buildPreprocessPasses(const std::string& filename, std::vector<std::string>& args, size_t& current_index) {
    std::vector<Pass> passes;

    // every control flow body a block, and the explicit instantiation declarations dropped, see
    // dropInstantiableExternTemplates
    passes.push_back({{"insert-braces", "Add braces around single statement control flow bodies", false},
        [&](const std::string& source, unsigned, bool&) { return insertBraces(dropInstantiableExternTemplates(source, filename, args)); }});

    // every member and base public. When that doesn't compile, eg. as a static_assert checks that a
    // destructor is protected or a base private, the special members and the bases keep their access.
    passes.push_back({{"access-specifier", "Make every class member public", false},
        [&](const std::string& source, unsigned, bool&) {
            auto make_public = [&](bool keep_observable_access) {
                return rewrite(source, filename, args, [keep_observable_access](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                    if (ctx.getLangOpts().CPlusPlus)
                        AccessSpecifierVisitor(ctx, rewriter, keep_observable_access).TraverseDecl(ctx.getTranslationUnitDecl());
                });
            };
            std::string result = make_public(false);
            return isCompilable(result, filename, args) ? result : make_public(true);
        }});

    // `template <class = void>` named, which a specialization's text refers to
    passes.push_back({{"name-template-type-parm", "Name unnamed template type parameters", false},
        [&](const std::string& source, unsigned, bool&) { return visitOnce<NameTemplateTypeParmVisitor>(source, filename, args); }});

    // a lambda a struct, whose type a copy or a specialization can name, innermost first
    passes.push_back({{"materialize-lambda", "Turn lambdas into named structs", true},
        [&](const std::string& source, unsigned, bool& changed) {
            return rewrite(source, filename, args, [&changed](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                MaterializeLambdaVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                changed = visitor.changed();
            });
        }});

    // every function declared before any definition, the definitions written at the end
    passes.push_back({{"decl-before-def", "Declare every function before any definition, giving template instantiations copies", false},
        [&](const std::string& source, unsigned, bool&) { return declBeforeDef(source, filename, args); }});

    // for, if and switch in a block, which keeps what the passes put before them in their scope,
    // and the body of a switch in a block, which mark-switch-case adds to
    passes.push_back({{"compound-stmt-wrapper", "Wrap for, if and switch statements in a block", false},
        [&](const std::string& source, unsigned, bool&) { return visitOnce<CompoundStmtWrapperVisitor>(source, filename, args); }});

    // an empty statement after each case label, so that hoist-calls puts a temporary under it
    passes.push_back({{"mark-switch-case", "Start the statements of every case label with an empty statement", false},
        [&](const std::string& source, unsigned, bool&) { return visitOnce<MarkSwitchCaseVisitor>(source, filename, args); }});

    // the comma operator parenthesized, so that what is moved out of an operand groups the same
    passes.push_back({{"paren-expr-wrapper", "Parenthesize comma operator expressions", false},
        [&](const std::string& source, unsigned, bool&) { return visitOnce<ParenExprWrapperVisitor>(source, filename, args); }});

    // a range-based for the iterator loop it stands for, whose calls can be hoisted
    passes.push_back({{"disassemble-cxx-for-range-stmt", "Rewrite range based for loops as for loops", false},
        [&](const std::string& source, unsigned, bool&) { return visitOnce<DisassembleCXXForRangeStmtVisitor>(source, filename, args); }});

    // a loop's condition and increment made statements of its body, whose calls can be hoisted
    passes.push_back({{"loop-to-ir", "Rewrite loops into their IR form", false},
        [&](const std::string& source, unsigned, bool&) { return visitOnce<LoopToIRVisitor>(source, filename, args); }});

    // one declaration per variable
    passes.push_back({{"comma-init", "Split comma separated declarations into one declaration each", false},
        [&](const std::string& source, unsigned, bool&) { return visitOnce<CommaInitVisitor>(source, filename, args); }});

    // &&, || and ?: with calls spelled out as ifs, so that hoisting a call keeps it conditional
    passes.push_back({{"short-circuit-to-ir", "Rewrite && and || into their IR form", true},
        [&](const std::string& source, unsigned iteration, bool& changed) {
            if (iteration == 0) current_index = 0;
            return rewrite(source, filename, args, [&](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                ShortCircuitOperatorToIRVisitor visitor(ctx, rewriter, current_index);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                changed = visitor.didConvertToIR();
            });
        }});

    // a constant expression the number it is
    passes.push_back({{"constant-fold", "Fold constant expressions", true},
        [&](const std::string& source, unsigned, bool& changed) {
            return rewrite(source, filename, args, [&changed](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
                ConstantFoldVisitor visitor(ctx, rewriter);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                changed = visitor.didFold();
            });
        }});

    // every nested call a statement of its own, its result in a temporary
    passes.push_back({{"hoist-calls", "Hoist nested function calls into temporaries", true},
        [&](const std::string& source, unsigned iteration, bool& changed) {
            if (iteration == 0) current_index = 0;
            return rewrite(source, filename, args, [&](clang::ASTContext& ctx, clang::Sema& sema, clang::Rewriter& rewriter) {
                FuncCallCleanupVisitor visitor(ctx, sema, rewriter, current_index);
                visitor.TraverseDecl(ctx.getTranslationUnitDecl());
                changed = visitor.didHoist();
            });
        }});

    return passes;
}

/// The number of lines of @p text, for PassStats.
size_t countLines(const std::string& text) {
    return std::count(text.begin(), text.end(), '\n');
}

}  // namespace

// the same list the pipeline builds, without a TU to run it on
std::vector<PassInfo> preprocessPasses() {
    std::vector<std::string> args;
    size_t current_index = 0;
    std::vector<PassInfo> infos;
    for (const Pass& pass : buildPreprocessPasses("", args, current_index))
        infos.push_back(pass.info);
    return infos;
}

namespace {

/// Runs the preprocess passes in order. Every iteration's output must compile, and when it doesn't,
/// what the errors are in is left as written and the pass runs again. Who leaves it depends on who
/// broke it:
///  - decl-before-def retries itself, see declBeforeDef;
///  - when a later pass needs what decl-before-def took away, eg. a template it reduced to a
///    declaration, decl-before-def is told to leave it (laterUnmoved) and every pass from it on
///    runs again, see rerunFromDeclBeforeDef;
///  - otherwise the pass itself leaves the functions around the errors as written (passSkips).
/// See docs/DESIGN.md section 4. The three sets are global, as the visitors that read them are
/// deep in the passes: laterUnmoved() and strippedTemplates() hold for the TU, passSkips() for
/// the pass being run.
class Pipeline {
  public:
    Pipeline(const std::string& filename, std::vector<std::string>& args, const PipelineOptions& options)
        : m_filename(filename), m_args(args), m_options(options), m_passes(buildPreprocessPasses(filename, args, m_current_index)) {}

    PipelineResult run(const std::string& source);

  private:
    /// What became of an iteration of a pass: its output is accepted, the passes are run again
    /// from decl-before-def, or the pipeline fails.
    enum class Outcome { Accepted, Rerun, Failed };

    Outcome runIteration(const Pass& pass, PassStats& stats, bool& changed);
    /// Leaves @p keys as written from decl-before-def on, see neededAsWritten, and has the passes
    /// run again from there, unless they are known or too many already are.
    bool rerunFromDeclBeforeDef(const Pass& pass, const std::set<std::string>& keys);
    bool rerunFromDeclBeforeDef(const Pass& pass, const std::string& code) {
        return rerunFromDeclBeforeDef(pass, neededAsWritten(code, m_filename, m_args, lastCompileFailure().diagnostics));
    }

    const std::string& m_filename;
    std::vector<std::string>& m_args;
    const PipelineOptions& m_options;
    size_t m_current_index = 0;
    std::vector<Pass> m_passes;
    PipelineResult m_out;
    /// the pass being run, and decl-before-def and its input
    size_t m_pass = 0;
    size_t m_decl_before_def = 0;
    std::string m_before_decl_before_def;
};

// Each pass runs until it changes nothing, if it repeats, then the next. A rerun from
// decl-before-def jumps m_pass back, see rerunFromDeclBeforeDef, and the loop carries on from there.
PipelineResult Pipeline::run(const std::string& source) {
    m_out.source = source;
    m_decl_before_def = m_passes.size();
    // what a previous preprocess() of this process learned is about another TU
    laterUnmoved().clear();
    strippedTemplates().clear();
    for (m_pass = 0; m_pass < m_passes.size(); ++m_pass) {
        const Pass& pass = m_passes[m_pass];
        if (pass.info.name == "decl-before-def") {
            m_before_decl_before_def = m_out.source;
            m_decl_before_def = m_pass;
        }
        if (m_options.on_pass_start) m_options.on_pass_start(pass.info, m_pass, m_passes.size());

        PassStats stats;
        stats.name = pass.info.name;
        stats.lines_before = countLines(m_out.source);
        auto start = std::chrono::steady_clock::now();
        // what an earlier pass left as written is the next pass's to rewrite
        passSkips().clear();
        Outcome outcome;
        bool changed;
        do outcome = runIteration(pass, stats, changed);
        while (outcome == Outcome::Accepted && pass.info.repeats && changed);
        if (outcome == Outcome::Rerun) continue;

        stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        stats.lines_after = countLines(m_out.source);
        m_out.passes.push_back(stats);
        if (outcome == Outcome::Failed) return m_out;
        if (m_options.on_pass_end) m_options.on_pass_end(stats, m_out.source);

        // what the passes after decl-before-def instantiate of a template it reduced to a declaration
        if (m_pass + 1 == m_passes.size() && rerunFromDeclBeforeDef(pass, strippedTemplatesUsedIn(m_out.source, m_filename, m_args)))
            continue;
        if (pass.info.name == m_options.stop_after) break;
    }
    return m_out;
}

// One iteration of @p pass, checked. Output that doesn't compile is recovered from in this order:
//  1. a rerun from decl-before-def, when the errors show it took away what the output needs
//     (section 4.1);
//  2. the functions around the errors left as written and the pass run again, up to 6 times
//     (section 4.3);
//  3. a rerun from decl-before-def again, for what the errors still need;
// and otherwise the pipeline fails here.
Pipeline::Outcome Pipeline::runIteration(const Pass& pass, PassStats& stats, bool& changed) {
    llvm::PrettyStackTraceFormat crash_note("running pass '%s', iteration %u", pass.info.name.c_str(), stats.iterations + 1);
    changed = false;
    std::string next = pass.run(m_out.source, stats.iterations, changed);
    bool compiles = isCompilable(next, m_filename, m_args);
    if (!compiles && rerunFromDeclBeforeDef(pass, next)) return Outcome::Rerun;

    // the functions around what still doesn't compile are left as written, and the pass run again;
    // the first pass and decl-before-def, which retries itself, can't
    bool leaves_functions = m_pass > 0 && pass.info.name != "decl-before-def";
    for (unsigned attempt = 0; !compiles && leaves_functions && attempt < 6; ++attempt) {
        size_t skipped = passSkips().size();
        for (const std::string& name : functionsAround(next, m_filename, m_args, lastCompileFailure().diagnostics))
            passSkips().insert(name);
        verbose() << pass.info.name << ": attempt " << attempt + 1 << " leaves " << passSkips().size() << " functions as written\n";
        if (passSkips().size() == skipped) break;
        changed = false;
        next = pass.run(m_out.source, stats.iterations, changed);
        compiles = isCompilable(next, m_filename, m_args);
    }

    ++stats.iterations;
    // the errors the fallback couldn't place may still be decl-before-def's to fix
    if (!compiles) {
        if (rerunFromDeclBeforeDef(pass, next)) return Outcome::Rerun;
        m_out.succeeded = false;
        m_out.failed_pass = pass.info.name;
        m_out.failed_iteration = stats.iterations;
        return Outcome::Failed;
    }
    m_out.source = std::move(next);
    // a repeating pass that never settles fails, with --max-iterations
    if (m_options.on_iteration) m_options.on_iteration(pass.info, stats.iterations);
    if (changed && m_options.max_iterations && stats.iterations >= m_options.max_iterations) {
        m_out.succeeded = false;
        m_out.did_not_converge = true;
        m_out.failed_pass = pass.info.name;
        m_out.failed_iteration = stats.iterations;
        return Outcome::Failed;
    }
    return Outcome::Accepted;
}

// A later pass can write code that needs what decl-before-def took away. eg. short-circuit-to-ir
// turns `dir d(argc > 1 ? b.sub(1) : b);` into ifs that assign `__short_circuit_result_0 = b.sub(1);`.
// That assignment is a call of `str<int>::operator=(str &&)`, whose `noexcept(traits<T>::nothrow())`
// needs the body of `nothrow`, but decl-before-def had reduced `nothrow`'s template to a
// declaration, as nothing used it when it ran. So @p keys, here `nothrow`, are added to
// laterUnmoved(), the source goes back to what it was before decl-before-def, and the passes run
// again from there, decl-before-def now leaving them as written.
//
// Returns whether it reruns. It doesn't when the failing pass isn't after decl-before-def, when
// @p keys add nothing new, as rerunning again wouldn't change anything, or when 100 keys already
// are, after which only the pass's own fallback is left.
bool Pipeline::rerunFromDeclBeforeDef(const Pass& pass, const std::set<std::string>& keys) {
    size_t known = laterUnmoved().size();
    if (m_pass <= m_decl_before_def || known >= 100) return false;
    laterUnmoved().insert(keys.begin(), keys.end());
    if (laterUnmoved().size() == known) return false;
    verbose() << pass.info.name << ": " << laterUnmoved().size() << " left as written from decl-before-def on\n";
    m_out.source = m_before_decl_before_def;
    m_out.passes.resize(m_decl_before_def);
    m_pass = m_decl_before_def - 1;
    return true;
}

}  // namespace

PipelineResult preprocess(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                          const PipelineOptions& options) {
    // the passes parse with the user's flags and addPipelineFlags', and check as strictly as a
    // compile when the input allows it, see checkAsCompiled
    addPipelineFlags(args);
    checkAsCompiled(args, source, filename);
    return Pipeline(filename, args, options).run(source);
}

// The info mode, a debugging aid: braces inserted, then InformationCollectorVisitor prints what
// clang knows of every function to stdout; the source itself comes back unchanged.
bool collectInfo(const std::string& source, const std::string& filename, std::vector<std::string>& args, std::string& result) {
    addPipelineFlags(args);
    result = rewrite(insertBraces(source), filename, args, [](clang::ASTContext& ctx, clang::Sema&, clang::Rewriter& rewriter) {
        if (ctx.getLangOpts().CPlusPlus) {
            InformationCollectorVisitor visitor(ctx, rewriter);
            visitor.TraverseDecl(ctx.getTranslationUnitDecl());
        }
    });
    return isCompilable(result, filename, args);
}

// ─── optimize ──────────────────────────────────────────────────────

// The optimize mode: rounds of constant-fold, dce, copy-propagate, constant-fold, dce and
// dead-store-elim, until a whole round changes nothing; each one is checked to compile.
OptimizeResult oneForAll(const std::string& source, const std::string& filename, std::vector<std::string>& args,
                         const std::function<void(unsigned iteration)>& on_iteration) {
    addPipelineFlags(args);

    using Optimization = std::string (*)(const std::string&, const std::string&, std::vector<std::string>&, bool*, bool*);
    static const std::pair<const char*, Optimization> order[] = {
        {"constant-fold", constant_fold}, {"dce", deadCodeElim}, {"copy-propagate", copyPropagation},
        {"constant-fold", constant_fold}, {"dce", deadCodeElim}, {"dead-store-elim", deadStoreElim},
    };

    OptimizeResult out;
    out.source = source;
    bool did_change = true;
    while (did_change) {
        did_change = false;
        for (const auto& [name, optimization] : order) {
            bool is_compilable = true;
            llvm::PrettyStackTraceFormat crash_note("running optimization '%s', iteration %u", name, out.iterations + 1);
            std::string result = optimization(out.source, filename, args, &did_change, &is_compilable);
            if (!is_compilable) {
                out.succeeded = false;
                out.failed_pass = name;
                return out;
            }
            out.source = std::move(result);
        }
        out.iterations++;
        if (on_iteration) on_iteration(out.iterations);
    }
    return out;
}

}  // namespace invisible

