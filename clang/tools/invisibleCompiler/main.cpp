// The command line driver of invisible: parses `invisible <mode> <input> [options] [clang flags]`,
// checks that the input compiles, runs the mode, prints progress and writes the result, eg.
//
//     invisible preprocess b.cc -x c++ -std=c++23 --save-temps --time-passes
//
// The transformations themselves are in invisible.cpp and the passes' directories.

#include "invisible.h"
#include "helpers.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {

// ─── Exit codes ────────────────────────────────────────────────────

// what a script running invisible over a dataset tells apart: 3 is the input's fault, 1 a pass's

enum ExitCode : int {
    Success = 0,
    TransformFailed = 1,  ///< a pass produced code that doesn't compile, or didn't converge
    UsageError = 2,       ///< bad command line, or a file couldn't be read or written
    InputError = 3,       ///< the input doesn't compile to begin with
};

// ─── Modes ─────────────────────────────────────────────────────────

/// How a mode is run, see Driver::run.
enum class ModeKind { Preprocess, Info, SinglePass, Optimize, Inline };

/// An optimization run once, eg. invisible::constant_fold: (source, filename, args, changed, compiles).
using SinglePassFn = std::string (*)(const std::string&, const std::string&, std::vector<std::string>&, bool*, bool*);

/// A mode of the command line, the first positional argument, eg. `preprocess`.
struct Mode {
    const char* name;
    const char* description;
    ModeKind kind;
    const char* default_output;  ///< `-` for stdout
    SinglePassFn run = nullptr;
};

// the modes, in the order --help lists them
const Mode modes[] = {
    {"preprocess", "Run the preprocess pipeline (see --list-passes)", ModeKind::Preprocess, "a_couple_passes.cpp"},
    {"info", "Run the information collector", ModeKind::Info, "a_couple_passes.cpp"},
    {"constant-fold", "Fold constant expressions", ModeKind::SinglePass, "-", invisible::constant_fold},
    {"dce", "Eliminate dead code", ModeKind::SinglePass, "-", invisible::deadCodeElim},
    {"copy-propagate", "Propagate copies", ModeKind::SinglePass, "-", invisible::copyPropagation},
    {"dead-store-elim", "Eliminate dead stores", ModeKind::SinglePass, "-", invisible::deadStoreElim},
    {"optimize", "Repeat every optimization until none changes the source", ModeKind::Optimize, "optimized.cpp"},
    {"inline", "Inline the call sites listed in the oracle file", ModeKind::Inline, "inlined.cpp"},
};

/// The mode named @p name, or null.
const Mode* findMode(llvm::StringRef name) {
    for (const Mode& mode : modes)
        if (name == mode.name) return &mode;
    return nullptr;
}

// ─── Output helpers ────────────────────────────────────────────────

// Every message invisible prints itself starts `invisible: error:`, `warning:` or `note:`, like
// clang's, colored as clang colors them when --color allows.
llvm::ColorMode colorMode() {
    return invisible::settings().color ? llvm::ColorMode::Enable : llvm::ColorMode::Disable;
}

llvm::raw_ostream& error() {
    return llvm::WithColor(llvm::errs(), llvm::HighlightColor::Error, colorMode()).get() << "invisible: error: ";
}

llvm::raw_ostream& warning() {
    return llvm::WithColor(llvm::errs(), llvm::HighlightColor::Warning, colorMode()).get() << "invisible: warning: ";
}

llvm::raw_ostream& note() {
    return llvm::WithColor(llvm::errs(), llvm::HighlightColor::Note, colorMode()).get() << "invisible: note: ";
}

/// Prints @p text in @p color, eg. a pass's `ok` or `FAILED`.
void printColored(llvm::raw_ostream& os, llvm::raw_ostream::Colors color, llvm::StringRef text, bool bold = true) {
    llvm::WithColor(os, color, bold, false, colorMode()) << text;
}

/// `2.31s` under a minute, `4m12s` from one.
std::string formatSeconds(double seconds) {
    if (seconds < 60) return llvm::formatv("{0:f2}s", seconds).str();
    return llvm::formatv("{0}m{1:f0}s", unsigned(seconds) / 60, seconds - 60 * (unsigned(seconds) / 60)).str();
}

/// `1 iteration`, `3 iterations`.
std::string plural(size_t count, llvm::StringRef word) {
    return std::to_string(count) + " " + word.str() + (count == 1 ? "" : "s");
}

/// Quote an argument for a shell command line, when it needs it, eg. for the reproduction command
/// printed after a failure: `-DNAME="a b"` becomes `'-DNAME="a b"'`.
std::string shellQuote(llvm::StringRef arg) {
    if (!arg.empty() && arg.find_first_of(" \t\"'\\$`!*?[]{}()<>|&;#~") == llvm::StringRef::npos) return arg.str();
    std::string quoted = "'";
    for (char c : arg) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return quoted + "'";
}

/// The text of @p path, `-` for stdin, or nothing after reporting why it couldn't be read.
std::optional<std::string> readFile(llvm::StringRef path) {
    auto buffer = llvm::MemoryBuffer::getFileOrSTDIN(path, /*IsText=*/true);
    if (!buffer) {
        error() << "cannot read '" << path << "': " << buffer.getError().message() << "\n";
        return std::nullopt;
    }
    std::string text = (*buffer)->getBuffer().str();
    // passes rely on every line, including the last, ending in a newline
    if (!text.empty() && text.back() != '\n') text += '\n';
    return text;
}

/// Writes @p text to @p path, `-` for stdout; reports and returns false when it can't.
bool writeFile(llvm::StringRef path, llvm::StringRef text) {
    if (path == "-") {
        llvm::outs() << text;
        llvm::outs().flush();
        return true;
    }
    std::error_code ec;
    llvm::raw_fd_ostream out(path, ec, llvm::sys::fs::OF_Text);
    if (ec) {
        error() << "cannot write '" << path << "': " << ec.message() << "\n";
        return false;
    }
    out << text;
    return true;
}

// ─── Command line ──────────────────────────────────────────────────

/// The parsed command line: the mode, the input, invisible's options, and every other argument,
/// which is clang's.
struct CommandLine {
    std::string mode;
    std::string input;
    std::string output;
    std::string stop_after;
    std::string save_temps;
    std::string oracle = "oracle_list.txt";
    std::string color = "auto";
    unsigned max_iterations = 0;
    bool time_passes = false;
    bool quiet = false;
    bool verbose = false;
    bool list_passes = false;
    bool help = false;
    std::vector<std::string> clang_flags;
};

/// The text of --help, also printed when no mode is given.
void printHelp(llvm::raw_ostream& os) {
    os << "OVERVIEW: invisible - source to source C/C++ transformations\n\n"
          "USAGE: invisible <mode> <input> [options] [clang flags...] [-- clang flags...]\n\n"
          "  <input> is a single translation unit, `-` reads stdin. Arguments that aren't\n"
          "  options of invisible are passed to clang, eg. -x c++ -std=c++23.\n\n"
          "MODES:\n";
    for (const Mode& mode : modes) {
        os << "  ";
        os.indent(0) << llvm::left_justify(mode.name, 18) << mode.description;
        if (llvm::StringRef(mode.default_output) != "-") os << " (writes " << mode.default_output << ")";
        os << "\n";
    }
    os << "\nOPTIONS:\n"
          "  -o <file>               Write the result to <file>, `-` for stdout\n"
          "  --stop-after=<pass>     preprocess: stop after <pass>\n"
          "  --save-temps[=<dir>]    preprocess: write the output of every pass to <dir>\n"
          "                          (default invisible-temps)\n"
          "  --max-iterations=<n>    preprocess: fail a repeating pass after <n> iterations\n"
          "  --list-passes           List the preprocess passes and exit\n"
          "  --oracle=<file>         inline: the call sites to inline (default oracle_list.txt)\n"
          "  --time-passes           Print how long every pass took\n"
          "  --color=<when>          Color the output: auto, always or never (default auto)\n"
          "  -q, --quiet             Only print errors\n"
          "  -v, --verbose           Print what the passes do, and clang's diagnostics while parsing\n"
          "  -h, --help              Print this help\n"
          "\nEXIT CODES:\n"
          "  0  success\n"
          "  1  a pass produced code that doesn't compile, or didn't converge. The failing\n"
          "     code is written next to the output as <name>.failed.<ext>\n"
          "  2  bad command line, or a file couldn't be read or written\n"
          "  3  the input doesn't compile\n"
          "\nEXAMPLE:\n"
          "  invisible preprocess b.cc -x c++ -std=c++23 --save-temps --time-passes\n";
}

/// clang options whose value is the next argument, so that it isn't taken for the input.
bool clangOptionTakesValue(llvm::StringRef arg) {
    static const char* const options[] = {"-x", "-I", "-D", "-U", "-include", "-isystem", "-iquote", "-idirafter",
                                          "-Xclang", "-target", "-arch", "-imacros", "-isysroot", "--sysroot"};
    return llvm::is_contained(options, arg);
}

/// Parses the command line: invisible's own options anywhere, the first two arguments that aren't
/// options as the mode and the input, and everything else, or everything after `--`, for clang,
/// eg. `invisible preprocess a.cc -x c++ -std=c++23 -q` gives clang `-x c++ -std=c++23`.
/// Returns nothing after reporting a bad option.
std::optional<CommandLine> parseCommandLine(int argc, char** argv) {
    CommandLine cl;
    std::vector<llvm::StringRef> positional;
    bool only_clang_flags = false;

    // the value of `--name=value` or `--name value`
    auto value = [&](int& i, llvm::StringRef arg, llvm::StringRef name) -> std::optional<std::string> {
        if (arg.consume_front(name)) {
            if (arg.consume_front("=")) return arg.str();
            if (arg.empty() && i + 1 < argc) return std::string(argv[++i]);
        }
        error() << "option '" << name << "' needs a value\n";
        return std::nullopt;
    };

    for (int i = 1; i < argc; i++) {
        llvm::StringRef arg = argv[i];
        if (only_clang_flags) {
            cl.clang_flags.push_back(arg.str());
        } else if (arg == "--") {
            only_clang_flags = true;
        } else if (arg == "-h" || arg == "--help" || arg == "-help") {
            cl.help = true;
        } else if (arg == "-q" || arg == "--quiet") {
            cl.quiet = true;
        } else if (arg == "-v" || arg == "--verbose") {
            cl.verbose = true;
        } else if (arg == "--list-passes") {
            cl.list_passes = true;
        } else if (arg == "--time-passes") {
            cl.time_passes = true;
        } else if (arg == "--save-temps") {
            cl.save_temps = "invisible-temps";
        } else if (arg.starts_with("--save-temps=")) {
            cl.save_temps = arg.drop_front(strlen("--save-temps=")).str();
        } else if (arg == "-o" || arg.starts_with("-o=") || arg == "--output" || arg.starts_with("--output=")) {
            auto v = value(i, arg, arg.starts_with("--") ? "--output" : "-o");
            if (!v) return std::nullopt;
            cl.output = *v;
        } else if (arg == "--stop-after" || arg.starts_with("--stop-after=")) {
            auto v = value(i, arg, "--stop-after");
            if (!v) return std::nullopt;
            cl.stop_after = *v;
        } else if (arg == "--oracle" || arg.starts_with("--oracle=")) {
            auto v = value(i, arg, "--oracle");
            if (!v) return std::nullopt;
            cl.oracle = *v;
        } else if (arg == "--color" || arg.starts_with("--color=")) {
            auto v = value(i, arg, "--color");
            if (!v) return std::nullopt;
            if (*v != "auto" && *v != "always" && *v != "never") {
                error() << "--color must be auto, always or never, not '" << *v << "'\n";
                return std::nullopt;
            }
            cl.color = *v;
        } else if (arg == "--max-iterations" || arg.starts_with("--max-iterations=")) {
            auto v = value(i, arg, "--max-iterations");
            if (!v) return std::nullopt;
            if (!llvm::to_integer(*v, cl.max_iterations, 10)) {
                error() << "--max-iterations must be a number, not '" << *v << "'\n";
                return std::nullopt;
            }
        } else if (clangOptionTakesValue(arg) && i + 1 < argc) {
            cl.clang_flags.push_back(arg.str());
            cl.clang_flags.push_back(argv[++i]);
        } else if ((!arg.starts_with("-") || arg == "-") && positional.size() < 2) {
            positional.push_back(arg);
        } else {
            cl.clang_flags.push_back(arg.str());
        }
    }

    if (positional.size() > 0) cl.mode = positional[0].str();
    if (positional.size() > 1) cl.input = positional[1].str();

    // `preprocess <input> <flags> normal` and `... info` were how preprocess used to be run.
    if (!cl.clang_flags.empty() && (cl.clang_flags.back() == "normal" || cl.clang_flags.back() == "info")) {
        if (cl.clang_flags.back() == "info" && cl.mode == "preprocess") cl.mode = "info";
        cl.clang_flags.pop_back();
    }
    return cl;
}

// ─── Progress ──────────────────────────────────────────────────────

/// Prints one line per pass to stderr. On a terminal the line of the running
/// pass is updated in place with its iteration count.
class Progress {
  public:
    explicit Progress(bool quiet) : m_quiet(quiet), m_live(llvm::errs().is_displayed()) {}

    void start(const invisible::PassInfo& pass, size_t index, size_t count) {
        m_prefix = llvm::formatv("[{0,2}/{1}] {2}", index + 1, count, llvm::left_justify(pass.name, 24)).str();
        if (m_quiet || !m_live) return;
        llvm::errs() << "\r\33[2K" << m_prefix << " ...";
        llvm::errs().flush();
    }

    void iteration(unsigned iteration) {
        if (m_quiet || !m_live) return;
        llvm::errs() << "\r\33[2K" << m_prefix << " ... iteration " << iteration;
        llvm::errs().flush();
    }

    void finish(const invisible::PassStats& stats, bool ok) {
        if (m_quiet && ok) return;
        if (m_live) llvm::errs() << "\r\33[2K";
        llvm::errs() << m_prefix << " ";
        if (ok)
            printColored(llvm::errs(), llvm::raw_ostream::GREEN, "ok    ");
        else
            printColored(llvm::errs(), llvm::raw_ostream::RED, "FAILED");
        long delta = long(stats.lines_after) - long(stats.lines_before);
        llvm::errs() << llvm::formatv("  {0,-14}{1,8}", plural(stats.iterations, "iteration"), formatSeconds(stats.seconds));
        if (ok) llvm::errs() << llvm::formatv("  {0}{1} lines", delta >= 0 ? "+" : "", delta);
        llvm::errs() << "\n";
    }

  private:
    bool m_quiet;
    bool m_live;
    std::string m_prefix;
};

/// The table of --time-passes, the slowest pass first, eg.
///
///     pass                       time      %   iterations
///     hoist-calls              12.40s   41.2      37
void printTimings(const std::vector<invisible::PassStats>& passes) {
    double total = 0;
    for (const auto& stats : passes) total += stats.seconds;
    std::vector<const invisible::PassStats*> sorted;
    for (const auto& stats : passes) sorted.push_back(&stats);
    std::sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return a->seconds > b->seconds; });

    llvm::errs() << "\n  pass                       time      %   iterations\n";
    for (const auto* stats : sorted) {
        llvm::errs() << llvm::formatv("  {0,-24} {1,8}  {2,5:f1}  {3,6}\n", stats->name, formatSeconds(stats->seconds),
                                      total > 0 ? 100 * stats->seconds / total : 0.0, stats->iterations);
    }
    llvm::errs() << llvm::formatv("  {0,-24} {1,8}\n", "total", formatSeconds(total));
}

// ─── Running a mode ────────────────────────────────────────────────

/// Runs one mode on the input: reads it, checks it compiles, runs the mode, then writes the result
/// or reports the failure with the failing code and a command reproducing it.
class Driver {
  public:
    Driver(const CommandLine& cl, const Mode& mode) : m_cl(cl), m_mode(mode), m_progress(cl.quiet) {
        m_output = cl.output.empty() ? mode.default_output : cl.output;
        // clang picks the language from the extension when there is no -x, so the
        // intermediate files keep the input's.
        m_extension = llvm::sys::path::extension(cl.input).str();
        if (m_extension.empty()) m_extension = ".cc";
        m_failed_path = failedPath(m_output, m_extension);
    }

    /// The exit code of the mode, see ExitCode.
    int run() {
        auto source = readFile(m_cl.input);
        if (!source) return UsageError;

        switch (m_mode.kind) {
        case ModeKind::Preprocess: return runPreprocess(*source);
        case ModeKind::Info: return runInfo(*source);
        case ModeKind::SinglePass: return runSinglePass(*source);
        case ModeKind::Optimize: return runOptimize(*source);
        case ModeKind::Inline: return runInline(*source);
        }
        return UsageError;
    }

  private:
    /// Where the code of a failed pass goes. Clang is given this name too, so the
    /// diagnostics point into a file that exists.
    static std::string failedPath(llvm::StringRef output, llvm::StringRef extension) {
        if (output == "-") return ("invisible.failed" + extension).str();
        llvm::SmallString<256> path = llvm::sys::path::parent_path(output);
        llvm::sys::path::append(path, llvm::sys::path::stem(output) + ".failed" + extension);
        return std::string(path);
    }

    /// The flags a mode parses with, for the input check and the reproduction command.
    std::vector<std::string> flags(bool pipeline_flags) const {
        std::vector<std::string> args = m_cl.clang_flags;
        if (pipeline_flags) invisible::addPipelineFlags(args);
        return args;
    }

    /// Checks that the input compiles, so that a broken input isn't blamed on a pass.
    bool checkInput(const std::string& source, bool pipeline_flags) {
        std::string name = m_cl.input == "-" ? "stdin" + m_extension : m_cl.input;
        if (invisible::isCompilable(source, name, flags(pipeline_flags))) return true;
        const auto& failure = invisible::lastCompileFailure();
        llvm::errs() << failure.diagnostics;
        error() << name << " does not compile (" << plural(failure.errors, "error")
                << "); invisible needs a translation unit that compiles with the given flags\n";
        if (llvm::StringRef(failure.diagnostics).contains("file not found"))
            note() << "a header wasn't found; give invisible a preprocessed translation unit, eg. from "
                      "clang++ -E -P\n";
        return false;
    }

    /// Reports the last compile failure: clang's diagnostics, where the failing code
    /// was written and how to reproduce it.
    void reportCompileFailure(llvm::StringRef what) {
        const auto& failure = invisible::lastCompileFailure();
        llvm::errs() << failure.diagnostics;
        error() << what << " produced code that does not compile (" << plural(failure.errors, "error") << ")\n";
        if (!writeFile(failure.filename, failure.source)) return;
        note() << "the failing code is in " << failure.filename << "; reproduce with:\n";
        std::string command = "clang++ -fsyntax-only";
        for (const std::string& flag : m_last_flags) command += " " + shellQuote(flag);
        llvm::errs() << "    " << command << " " << shellQuote(failure.filename) << "\n";
    }

    /// Removes the failing code of an earlier run, which would mislead once this one succeeded.
    void removeStaleFailure() {
        if (llvm::sys::fs::exists(m_failed_path)) llvm::sys::fs::remove(m_failed_path);
    }

    /// Writes @p result to the output and says where, unless --quiet.
    int finishSuccess(const std::string& result, double seconds) {
        if (!writeFile(m_output, result)) return UsageError;
        removeStaleFailure();
        if (!m_cl.quiet && m_output != "-") {
            llvm::errs() << "invisible: wrote " << m_output << " ("
                         << plural(std::count(result.begin(), result.end(), '\n'), "line") << ") in " << formatSeconds(seconds) << "\n";
        }
        return Success;
    }

    /// The preprocess mode: the pipeline of invisible::preprocess, with a progress line per pass and,
    /// with --save-temps, the input and every pass's output written as `00-input.cc`, `01-insert-braces.cc`...
    int runPreprocess(const std::string& source) {
        auto start = std::chrono::steady_clock::now();
        m_last_flags = flags(true);
        if (!checkInput(source, true)) return InputError;

        std::vector<invisible::PassInfo> passes = invisible::preprocessPasses();
        if (!m_cl.stop_after.empty() &&
            std::none_of(passes.begin(), passes.end(), [&](const auto& pass) { return pass.name == m_cl.stop_after; })) {
            error() << "unknown pass '" << m_cl.stop_after << "' for --stop-after; see --list-passes\n";
            return UsageError;
        }
        if (!m_cl.save_temps.empty()) {
            if (auto ec = llvm::sys::fs::create_directories(m_cl.save_temps)) {
                error() << "cannot create '" << m_cl.save_temps << "': " << ec.message() << "\n";
                return UsageError;
            }
            if (!writeFile(tempPath(0, "input"), source)) return UsageError;
        }

        invisible::PipelineOptions options;
        options.stop_after = m_cl.stop_after;
        options.max_iterations = m_cl.max_iterations;
        size_t pass_index = 0;
        options.on_pass_start = [&](const invisible::PassInfo& pass, size_t index, size_t count) {
            pass_index = index;
            m_progress.start(pass, index, count);
        };
        options.on_iteration = [&](const invisible::PassInfo&, unsigned iteration) { m_progress.iteration(iteration); };
        options.on_pass_end = [&](const invisible::PassStats& stats, const std::string& result) {
            m_progress.finish(stats, true);
            if (!m_cl.save_temps.empty()) writeFile(tempPath(pass_index + 1, stats.name), result);
        };

        std::vector<std::string> args = m_cl.clang_flags;
        invisible::PipelineResult result = invisible::preprocess(source, m_failed_path, args, options);

        if (!result.succeeded) {
            m_progress.finish(result.passes.back(), false);
            if (m_cl.time_passes) printTimings(result.passes);
            if (result.did_not_converge) {
                error() << "pass '" << result.failed_pass << "' was still changing the code after "
                        << plural(result.failed_iteration, "iteration") << " (--max-iterations)\n";
                if (writeFile(m_failed_path, result.source)) note() << "its latest output is in " << m_failed_path << "\n";
            } else {
                reportCompileFailure(llvm::formatv("pass '{0}' (iteration {1})", result.failed_pass, result.failed_iteration).str());
            }
            note() << m_output << " was not written";
            if (m_cl.save_temps.empty()) llvm::errs() << "; rerun with --save-temps to keep the output of every pass";
            else llvm::errs() << "; the output of every pass that succeeded is in " << m_cl.save_temps;
            llvm::errs() << "\n";
            return TransformFailed;
        }

        if (m_cl.time_passes) printTimings(result.passes);
        return finishSuccess(result.source, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    }

    /// Where --save-temps writes the output of the @p index -th pass, eg. `invisible-temps/05-decl-before-def.cc`.
    std::string tempPath(size_t index, llvm::StringRef name) const {
        llvm::SmallString<256> path(m_cl.save_temps);
        llvm::sys::path::append(path, llvm::formatv("{0}{1}-{2}{3}", index < 10 ? "0" : "", index, name, m_extension).str());
        return std::string(path);
    }

    /// The info mode, see invisible::collectInfo.
    int runInfo(const std::string& source) {
        auto start = std::chrono::steady_clock::now();
        m_last_flags = flags(true);
        if (!checkInput(source, true)) return InputError;
        std::vector<std::string> args = m_cl.clang_flags;
        std::string result;
        if (!invisible::collectInfo(source, m_failed_path, args, result)) {
            reportCompileFailure("the information collector");
            return TransformFailed;
        }
        return finishSuccess(result, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    }

    /// One optimization run once, eg. `invisible constant-fold a.cc`, printed to stdout by default.
    int runSinglePass(const std::string& source) {
        auto start = std::chrono::steady_clock::now();
        m_last_flags = flags(false);
        if (!checkInput(source, false)) return InputError;

        invisible::PassInfo info{m_mode.name, m_mode.description, true};
        invisible::PassStats stats;
        stats.name = m_mode.name;
        stats.iterations = 1;
        stats.lines_before = std::count(source.begin(), source.end(), '\n');
        m_progress.start(info, 0, 1);

        std::vector<std::string> args = m_cl.clang_flags;
        bool is_compilable = true;
        std::string result = m_mode.run(source, m_failed_path, args, nullptr, &is_compilable);
        stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        stats.lines_after = std::count(result.begin(), result.end(), '\n');

        if (!is_compilable) {
            m_progress.finish(stats, false);
            reportCompileFailure(llvm::formatv("pass '{0}'", m_mode.name).str());
            note() << "no output was written\n";
            return TransformFailed;
        }
        m_progress.finish(stats, true);
        if (m_output == "-") {
            // kept from before: the result is followed by an empty line
            llvm::outs() << result << "\n";
            removeStaleFailure();
            return Success;
        }
        return finishSuccess(result, stats.seconds);
    }

    /// The optimize mode, see invisible::oneForAll.
    int runOptimize(const std::string& source) {
        auto start = std::chrono::steady_clock::now();
        m_last_flags = flags(true);
        if (!checkInput(source, true)) return InputError;

        std::vector<std::string> args = m_cl.clang_flags;
        invisible::OptimizeResult result = invisible::oneForAll(source, m_failed_path, args, [&](unsigned iteration) {
            if (!m_cl.quiet) llvm::errs() << "[optimize] iteration " << iteration << " done\n";
        });
        if (!result.succeeded) {
            reportCompileFailure(llvm::formatv("optimization '{0}' (iteration {1})", result.failed_pass, result.iterations + 1).str());
            note() << m_output << " was not written\n";
            return TransformFailed;
        }
        if (!m_cl.quiet) llvm::errs() << "[optimize] converged after " << plural(result.iterations, "iteration") << "\n";
        return finishSuccess(result.source, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    }

    /// The inline mode: reads the oracle, the call sites clang -O2 inlined, eg.
    ///
    ///     _Z5totalR3VecIiE _Z6squarei 28
    ///
    /// and has invisible::inline_functions paste each callee's body at its call, see docs/DESIGN.md
    /// section 8. It fails, exit code 1, unless every site was inlined, but writes the result anyway.
    int runInline(const std::string& source) {
        auto start = std::chrono::steady_clock::now();
        m_last_flags = flags(true);
        auto oracle_text = readFile(m_cl.oracle);
        if (!oracle_text) return UsageError;

        // one entry per line: <caller> <callee> <line>. A fourth field, the lines of the sites of a
        // call LLVM found in a body it had inlined first, which some oracles list, is ignored
        std::vector<invisible::InlineEntry> entries;
        llvm::SmallVector<llvm::StringRef> lines;
        llvm::StringRef(*oracle_text).split(lines, '\n');
        for (size_t i = 0; i < lines.size(); i++) {
            llvm::SmallVector<llvm::StringRef> fields;
            lines[i].split(fields, ' ', -1, /*KeepEmpty=*/false);
            if (fields.empty()) continue;
            invisible::InlineEntry entry;
            if ((fields.size() != 3 && fields.size() != 4) || fields[2].getAsInteger(10, entry.line)) {
                error() << m_cl.oracle << ":" << i + 1 << ": expected '<caller> <callee> <line>'\n";
                return UsageError;
            }
            entry.caller = fields[0].str();
            entry.callee = fields[1].str();
            entries.push_back(std::move(entry));
        }

        if (!checkInput(source, true)) return InputError;
        std::vector<std::string> args = m_cl.clang_flags;
        invisible::InlineResult result = invisible::inline_functions(source, m_failed_path, entries, args);
        size_t inlined = std::count(result.succeeded.begin(), result.succeeded.end(), true);
        if (!m_cl.quiet) llvm::errs() << "invisible: inlined " << inlined << " of " << plural(entries.size(), "call site") << "\n";
        int status = finishSuccess(result.source, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        if (status != Success) return status;
        return inlined == entries.size() ? Success : TransformFailed;
    }

    const CommandLine& m_cl;
    const Mode& m_mode;
    Progress m_progress;
    std::string m_output;
    std::string m_extension;
    std::string m_failed_path;
    std::vector<std::string> m_last_flags;
};

/// The list of --list-passes.
void printPasses() {
    auto passes = invisible::preprocessPasses();
    llvm::outs() << "The preprocess passes, in the order they run:\n\n";
    for (size_t i = 0; i < passes.size(); i++) {
        llvm::outs() << llvm::formatv("  {0,2}. {1} {2}{3}\n", i + 1, llvm::left_justify(passes[i].name, 24), passes[i].description,
                                      passes[i].repeats ? " (repeats until nothing changes)" : "");
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    // a crash prints a stack trace, with the pass and iteration a PrettyStackTraceFormat names
    llvm::InitLLVM init(argc, argv);

    auto cl = parseCommandLine(argc, argv);
    if (!cl) return UsageError;

    auto& settings = invisible::settings();
    settings.verbose = cl->verbose;
    settings.color = cl->color == "always" || (cl->color == "auto" && llvm::errs().has_colors());

    if (cl->help) {
        printHelp(llvm::outs());
        return Success;
    }
    if (cl->list_passes) {
        printPasses();
        return Success;
    }
    if (cl->mode.empty()) {
        printHelp(llvm::errs());
        return UsageError;
    }

    const Mode* mode = findMode(cl->mode);
    // an unknown mode suggests the closest known one, eg. `preprocss` -> `preprocess`
    if (!mode) {
        error() << "unknown mode '" << cl->mode << "'";
        const Mode* closest = nullptr;
        unsigned best = 4;
        for (const Mode& candidate : modes) {
            unsigned distance = llvm::StringRef(candidate.name).edit_distance(cl->mode);
            if (distance < best) best = distance, closest = &candidate;
        }
        if (closest) llvm::errs() << "; did you mean '" << closest->name << "'?";
        llvm::errs() << "\n";
        note() << "run 'invisible --help' to see the modes\n";
        return UsageError;
    }
    if (cl->input.empty()) {
        error() << "no input file; usage: invisible " << mode->name << " <input> [options] [clang flags...]\n";
        return UsageError;
    }
    if (mode->kind != ModeKind::Preprocess && (!cl->stop_after.empty() || !cl->save_temps.empty() || cl->max_iterations)) {
        error() << "--stop-after, --save-temps and --max-iterations only apply to preprocess\n";
        return UsageError;
    }
    if (cl->quiet && cl->verbose) warning() << "--quiet and --verbose both given; the passes' chatter is still shown\n";

    Driver driver(*cl, *mode);
    return driver.run();
}
