#include <cstdio>
#include <editline.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <string_view>

#include "box_ptr.hh"
#include "repl-interacter.hh"
#include "repl.hh"

#include "ansicolor.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-cache.hh"
#include "eval-inline.hh"
#include "eval-settings.hh"
#include "attr-path.hh"
#include "signals.hh"
#include "store-api.hh"
#include "log-store.hh"
#include "common-eval-args.hh"
#include "get-drvs.hh"
#include "derivations.hh"
#include "globals.hh"
#include "flake/flake.hh"
#include "flake/lockfile.hh"
#include "editor-for.hh"
#include "finally.hh"
#include "markdown.hh"
#include "local-fs-store.hh"
#include "signals.hh"
#include "print.hh"
#include "progress-bar.hh"
#include "gc-small-vector.hh"
#include "users.hh"

#if HAVE_BOEHMGC
#define GC_INCLUDE_NEW
#include <gc/gc_cpp.h>
#endif

// XXX: These are for nix-doc features and will be removed in a future rewrite where this functionality is integrated more natively.
extern "C" {
    char const *nd_get_function_docs(char const *filename, size_t line, size_t col);
    void nd_free_string(char const *str);
}

namespace nix {


/** Wrapper around std::unique_ptr with a custom deleter for strings from nix-doc **/
using NdString = std::unique_ptr<const char, decltype(&nd_free_string)>;

/**
 * Fetch a string representing the doc comment using nix-doc and wrap it in an RAII wrapper.
 */
NdString lambdaDocsForPos(SourcePath const path, nix::Pos const &pos) {
  std::string const file = path.to_string();
  return NdString{nd_get_function_docs(file.c_str(), pos.line, pos.column), &nd_free_string};
}

/**
 * Returned by `NixRepl::processLine`.
 */
enum class ProcessLineResult {
    /**
     * The user exited with `:quit`. The REPL should exit. The surrounding
     * program or evaluation (e.g., if the REPL was acting as the debugger)
     * should also exit.
     */
    Quit,
    /**
     * The user exited with `:continue`. The REPL should exit, but the program
     * should continue running.
     */
    Continue,
    /**
     * The user did not exit. The REPL should request another line of input.
     */
    PromptAgain,
};

using namespace std::literals::string_view_literals;

struct NixRepl
    : AbstractNixRepl
    , detail::ReplCompleterMixin
    #if HAVE_BOEHMGC
    , gc
    #endif
{
    /* clang-format: off */
    static constexpr std::array COMMANDS = {
        "add"sv, "a"sv,
        "load"sv, "l"sv,
        "load-flake"sv, "lf"sv,
        "reload"sv, "r"sv,
        "edit"sv, "e"sv,
        "t"sv,
        "u"sv,
        "b"sv,
        "bl"sv,
        "i"sv,
        "sh"sv,
        "log"sv,
        "print"sv, "p"sv,
        "quit"sv, "q"sv,
        "doc"sv,
        "te"sv,
    };

    static constexpr std::array DEBUG_COMMANDS = {
        "env"sv,
        "bt"sv, "backtrace"sv,
        "st"sv,
        "c"sv, "continue"sv,
        "s"sv, "step"sv,
    };
    /* clang-format: on */

    size_t debugTraceIndex;

    Strings loadedFiles;
    std::function<AnnotatedValues()> getValues;

    // Uses 8MiB of memory. It's fine.
    const static int envSize = 1 << 20;
    std::shared_ptr<StaticEnv> staticEnv;
    Env * env;
    int displ;
    StringSet varNames;

    box_ptr<ReplInteracter> interacter;

    NixRepl(const SearchPath & searchPath, nix::ref<Store> store,ref<EvalState> state,
            std::function<AnnotatedValues()> getValues);
    virtual ~NixRepl() = default;

    ReplExitStatus mainLoop() override;
    void initEnv() override;

    virtual StringSet completePrefix(const std::string & prefix) override;

    /**
     * @exception nix::Error thrown directly if the expression does not evaluate
     * to a derivation, or evaluates to an invalid derivation.
     */
    StorePath getDerivationPath(Value & v);
    ProcessLineResult processLine(std::string line);

    void loadFile(const Path & path);
    void loadFlake(const std::string & flakeRef);
    void loadFiles();
    void reloadFiles();
    void addAttrsToScope(Value & attrs);
    void addVarToScope(const Symbol name, Value & v);
    Expr & parseString(std::string s);
    void evalString(std::string s, Value & v);
    void loadDebugTraceEnv(DebugTrace & dt);

    /**
     * Load the `repl-overlays` and add the resulting AttrSet to the top-level
     * bindings.
     */
    void loadReplOverlays();

    /**
     * Get a list of each of the `repl-overlays` (parsed and evaluated).
     */
    Value * replOverlays();

    /**
     * Get the Nix function that composes the `repl-overlays` together.
     */
    Value * getReplOverlaysEvalFunction();

    /**
     * Cached return value of `getReplOverlaysEvalFunction`.
     *
     * Note: This is `shared_ptr` to avoid garbage collection.
     */
    std::shared_ptr<Value *> replOverlaysEvalFunction =
    #if HAVE_BOEHMGC
        std::allocate_shared<Value *>(traceable_allocator<Value *>(), nullptr);
    #else
        std::make_shared<Value *>(nullptr);
    #endif

    /**
     * Get the `info` AttrSet that's passed as the first argument to each
     * of the `repl-overlays`.
     */
    Value * replInitInfo();

    /**
     * Get the current top-level bindings as an AttrSet.
     */
    Value * bindingsToAttrs();
    /**
     * Parse a file, evaluate its result, and force the resulting value.
     */
    Value * evalFile(SourcePath & path);

    void printValue(std::ostream & str,
                              Value & v,
                              unsigned int maxDepth = std::numeric_limits<unsigned int>::max())
    {
        ::nix::printValue(*state, str, v, PrintOptions {
            .ansiColors = true,
            .force = true,
            .derivationPaths = true,
            .maxDepth = maxDepth,
            .prettyIndent = 2,
            .errors = ErrorPrintBehavior::ThrowTopLevel,
        });
    }
};

std::string removeWhitespace(std::string s)
{
    s = chomp(s);
    size_t n = s.find_first_not_of(" \n\r\t");
    if (n != std::string::npos) s = std::string(s, n);
    return s;
}

static box_ptr<ReplInteracter> makeInteracter() {
    if (experimentalFeatureSettings.isEnabled(Xp::ReplAutomation))
        return make_box_ptr<AutomationInteracter>();
    else
        return make_box_ptr<ReadlineLikeInteracter>(getDataDir() + "/nix/repl-history");
}

NixRepl::NixRepl(const SearchPath & searchPath, nix::ref<Store> store, ref<EvalState> state,
            std::function<NixRepl::AnnotatedValues()> getValues)
    : AbstractNixRepl(state)
    , debugTraceIndex(0)
    , getValues(getValues)
    , staticEnv(new StaticEnv(nullptr, state->staticBaseEnv.get()))
    , interacter(makeInteracter())
{
}

void runNix(Path program, const Strings & args,
    const std::optional<std::string> & input = {})
{
    auto subprocessEnv = getEnv();
    subprocessEnv["NIX_CONFIG"] = globalConfig.toKeyValue();

    runProgram2(RunOptions {
        .program = settings.nixBinDir+ "/" + program,
        .args = args,
        .environment = subprocessEnv,
        .input = input,
    });

    return;
}

static std::ostream & showDebugTrace(std::ostream & out, const PosTable & positions, const DebugTrace & dt)
{
    if (dt.isError)
        out << ANSI_RED "error: " << ANSI_NORMAL;
    out << dt.hint.str() << "\n";

    // prefer direct pos, but if noPos then try the expr.
    auto pos = dt.pos
        ? dt.pos
        : positions[dt.expr.getPos() ? dt.expr.getPos() : noPos];

    if (pos) {
        out << *pos;
        if (auto loc = pos->getCodeLines()) {
            out << "\n";
            printCodeLines(out, "", *pos, *loc);
            out << "\n";
        }
    }

    return out;
}

static bool isFirstRepl = true;

ReplExitStatus NixRepl::mainLoop()
{
    if (isFirstRepl) {
        std::string_view debuggerNotice = "";
        if (state->debugRepl) {
            debuggerNotice = " debugger";
        }
        notice("Lix %1%%2%\nType :? for help.", nixVersion, debuggerNotice);
    }

    isFirstRepl = false;

    loadFiles();

    auto _guard = interacter->init(static_cast<detail::ReplCompleterMixin *>(this));

    /* Stop the progress bar because it interferes with the display of
       the repl. */
    stopProgressBar();

    std::string input;

    while (true) {
        _isInterrupted = false;

        // When continuing input from previous lines, don't print a prompt, just align to the same
        // number of chars as the prompt.
        if (!interacter->getLine(input, input.empty() ? ReplPromptType::ReplPrompt : ReplPromptType::ContinuationPrompt)) {
            // Ctrl-D should exit the debugger.
            state->debugStop = false;
            logger->cout("");
            // TODO: Should Ctrl-D exit just the current debugger session or
            // the entire program?
            return ReplExitStatus::QuitAll;
        }
        try {
            switch (processLine(input)) {
                case ProcessLineResult::Quit:
                    return ReplExitStatus::QuitAll;
                case ProcessLineResult::Continue:
                    return ReplExitStatus::Continue;
                case ProcessLineResult::PromptAgain:
                    break;
                default:
                    abort();
            }
        } catch (ParseError & e) {
            if (e.msg().find("unexpected end of file") != std::string::npos) {
                // For parse errors on incomplete input, we continue waiting for the next line of
                // input without clearing the input so far.
                continue;
            } else {
              printMsg(lvlError, e.msg());
            }
        } catch (EvalError & e) {
            printMsg(lvlError, e.msg());
        } catch (Error & e) {
            printMsg(lvlError, e.msg());
        } catch (Interrupted & e) {
            printMsg(lvlError, e.msg());
        }

        // We handled the current input fully, so we should clear it
        // and read brand new input.
        input.clear();
        std::cout << std::endl;
    }
}

StringSet NixRepl::completePrefix(const std::string & prefix)
{
    StringSet completions;

    // We should only complete colon commands if there's a colon at the beginning,
    // but editline (for... whatever reason) doesn't *give* us the colon in the
    // completion callback. If the user types :rel<TAB>, `prefix` will only be `rel`.
    // Luckily, editline provides a global variable for its current buffer, so we can
    // check for the presence of a colon there.
    if (rl_line_buffer != nullptr && rl_line_buffer[0] == ':') {
        for (auto const & colonCmd : this->COMMANDS) {
            if (colonCmd.starts_with(prefix)) {
                completions.insert(std::string(colonCmd));
            }
        }

        if (state->debugRepl) {
            for (auto const & colonCmd : this->DEBUG_COMMANDS) {
                if (colonCmd.starts_with(prefix)) {
                    completions.insert(std::string(colonCmd));
                }
            }
        }

        // If there were : command completions, then we should only return those,
        // because otherwise this is not valid Nix syntax.
        // However if we didn't get any completions, then this could be something
        // like `:b pkgs.hel<TAB>`, in which case we should do expression completion
        // as normal.
        if (!completions.empty()) {
            return completions;
        }
    }

    size_t start = prefix.find_last_of(" \n\r\t(){}[]");
    std::string prev, cur;
    if (start == std::string::npos) {
        prev = "";
        cur = prefix;
    } else {
        prev = std::string(prefix, 0, start + 1);
        cur = std::string(prefix, start + 1);
    }

    size_t slash, dot;

    if ((slash = cur.rfind('/')) != std::string::npos) {
        try {
            auto dir = std::string(cur, 0, slash);
            auto prefix2 = std::string(cur, slash + 1);
            for (auto & entry : readDirectory(dir == "" ? "/" : dir)) {
                if (entry.name[0] != '.' && entry.name.starts_with(prefix2))
                    completions.insert(prev + dir + "/" + entry.name);
            }
        } catch (Error &) {
        }
    } else if ((dot = cur.rfind('.')) == std::string::npos) {
        /* This is a variable name; look it up in the current scope. */
        StringSet::iterator i = varNames.lower_bound(cur);
        while (i != varNames.end()) {
            if (i->substr(0, cur.size()) != cur) break;
            completions.insert(prev + *i);
            i++;
        }
    } else {
        /* Temporarily disable the debugger, to avoid re-entering readline. */
        auto debug_repl = state->debugRepl;
        state->debugRepl = nullptr;
        Finally restoreDebug([&]() { state->debugRepl = debug_repl; });
        try {
            /* This is an expression that should evaluate to an
               attribute set.  Evaluate it to get the names of the
               attributes. */
            auto expr = cur.substr(0, dot);
            auto cur2 = cur.substr(dot + 1);

            Expr & e = parseString(expr);
            Value v;
            e.eval(*state, *env, v);
            state->forceAttrs(v, noPos, "while evaluating an attrset for the purpose of completion (this error should not be displayed; file an issue?)");

            for (auto & i : *v.attrs) {
                std::string_view name = state->symbols[i.name];
                if (name.substr(0, cur2.size()) != cur2) continue;
                completions.insert(concatStrings(prev, expr, ".", name));
            }

        } catch (ParseError & e) {
            // Quietly ignore parse errors.
        } catch (EvalError & e) {
            // Quietly ignore evaluation errors.
        } catch (BadURL & e) {
            // Quietly ignore BadURL flake-related errors.
        } catch (SysError & e) {
            // Quietly ignore system errors which can for example be raised by
            // a non-existent file being `import`-ed.
        }
    }

    return completions;
}


// FIXME: DRY and match or use the parser
static bool isVarName(std::string_view s)
{
    if (s.size() == 0) return false;
    char c = s[0];
    if ((c >= '0' && c <= '9') || c == '-' || c == '\'') return false;
    for (auto & i : s)
        if (!((i >= 'a' && i <= 'z') ||
              (i >= 'A' && i <= 'Z') ||
              (i >= '0' && i <= '9') ||
              i == '_' || i == '-' || i == '\''))
            return false;
    return true;
}


StorePath NixRepl::getDerivationPath(Value & v) {
    auto drvInfo = getDerivation(*state, v, false);
    if (!drvInfo)
        throw Error("expression does not evaluate to a derivation, so I can't build it");
    auto drvPath = drvInfo->queryDrvPath();
    if (!drvPath)
        throw Error("expression did not evaluate to a valid derivation (no 'drvPath' attribute)");
    if (!state->store->isValidPath(*drvPath))
        throw Error("expression evaluated to invalid derivation '%s'", state->store->printStorePath(*drvPath));
    return *drvPath;
}

void NixRepl::loadDebugTraceEnv(DebugTrace & dt)
{
    initEnv();

    auto se = state->getStaticEnv(dt.expr);
    if (se) {
        auto vm = mapStaticEnvBindings(state->symbols, *se.get(), dt.env);

        // add staticenv vars.
        for (auto & [name, value] : *(vm.get()))
            addVarToScope(state->symbols.create(name), *value);
    }
}

ProcessLineResult NixRepl::processLine(std::string line)
{
    line = trim(line);
    if (line.empty())
        return ProcessLineResult::PromptAgain;

    std::string command, arg;

    if (line[0] == ':') {
        size_t p = line.find_first_of(" \n\r\t");
        command = line.substr(0, p);
        if (p != std::string::npos) arg = removeWhitespace(line.substr(p));
    } else {
        arg = line;
    }

    if (command == ":?" || command == ":help") {
        // FIXME: convert to Markdown, include in the 'nix repl' manpage.
        std::cout
             << "The following commands are available:\n"
             << "\n"
             << "  <expr>                       Evaluate and print expression\n"
             << "  <x> = <expr>                 Bind expression to variable\n"
             << "  :a, :add <expr>              Add attributes from resulting set to scope\n"
             << "  :b <expr>                    Build a derivation\n"
             << "  :bl <expr>                   Build a derivation, creating GC roots in the\n"
             << "                               working directory\n"
             << "  :e, :edit <expr>             Open package or function in $EDITOR\n"
             << "  :i <expr>                    Build derivation, then install result into\n"
             << "                               current profile\n"
             << "  :l, :load <path>             Load Nix expression and add it to scope\n"
             << "  :lf, :load-flake <ref>       Load Nix flake and add it to scope\n"
             << "  :p, :print <expr>            Evaluate and print expression recursively\n"
             << "                               Strings are printed directly, without escaping.\n"
             << "  :q, :quit                    Exit nix-repl\n"
             << "  :r, :reload                  Reload all files\n"
             << "  :sh <expr>                   Build dependencies of derivation, then start\n"
             << "                               nix-shell\n"
             << "  :t <expr>                    Describe result of evaluation\n"
             << "  :u <expr>                    Build derivation, then start nix-shell\n"
             << "  :doc <expr>                  Show documentation for the provided function (experimental lambda support)\n"
             << "  :log <expr>                  Show logs for a derivation\n"
             << "  :te, :trace-enable [bool]    Enable, disable or toggle showing traces for\n"
             << "                               errors\n"
             << "  :?, :help                    Brings up this help menu\n"
             ;
        if (state->debugRepl) {
             std::cout
             << "\n"
             << "        Debug mode commands\n"
             << "  :env             Show env stack\n"
             << "  :bt, :backtrace  Show trace stack\n"
             << "  :st              Show current trace\n"
             << "  :st <idx>        Change to another trace in the stack\n"
             << "  :c, :continue    Go until end of program, exception, or builtins.break\n"
             << "  :s, :step        Go one step\n"
             ;
        }

    }

    else if (state->debugRepl && (command == ":bt" || command == ":backtrace")) {
        for (const auto & [idx, i] : enumerate(state->debugTraces)) {
            std::cout << "\n" << ANSI_BLUE << idx << ANSI_NORMAL << ": ";
            showDebugTrace(std::cout, state->positions, i);
        }
    }

    else if (state->debugRepl && (command == ":env")) {
        for (const auto & [idx, i] : enumerate(state->debugTraces)) {
            if (idx == debugTraceIndex) {
                printEnvBindings(*state, i.expr, i.env);
                break;
            }
        }
    }

    else if (state->debugRepl && (command == ":st")) {
        try {
            // change the DebugTrace index.
            debugTraceIndex = stoi(arg);
        } catch (...) { }

        for (const auto & [idx, i] : enumerate(state->debugTraces)) {
             if (idx == debugTraceIndex) {
                 std::cout << "\n" << ANSI_BLUE << idx << ANSI_NORMAL << ": ";
                 showDebugTrace(std::cout, state->positions, i);
                 std::cout << std::endl;
                 printEnvBindings(*state, i.expr, i.env);
                 loadDebugTraceEnv(i);
                 break;
             }
        }
    }

    else if (state->debugRepl && (command == ":s" || command == ":step")) {
        // set flag to stop at next DebugTrace; exit repl.
        state->debugStop = true;
        return ProcessLineResult::Continue;
    }

    else if (state->debugRepl && (command == ":c" || command == ":continue")) {
        // set flag to run to next breakpoint or end of program; exit repl.
        state->debugStop = false;
        return ProcessLineResult::Continue;
    }

    else if (command == ":a" || command == ":add") {
        Value v;
        evalString(arg, v);
        addAttrsToScope(v);
    }

    else if (command == ":l" || command == ":load") {
        state->resetFileCache();
        loadFile(arg);
    }

    else if (command == ":lf" || command == ":load-flake") {
        loadFlake(arg);
    }

    else if (command == ":r" || command == ":reload") {
        state->resetFileCache();
        reloadFiles();
    }

    else if (command == ":e" || command == ":edit") {
        Value v;
        evalString(arg, v);

        const auto [path, line] = [&] () -> std::pair<SourcePath, uint32_t> {
            if (v.type() == nPath || v.type() == nString) {
                NixStringContext context;
                auto path = state->coerceToPath(noPos, v, context, "while evaluating the filename to edit");
                return {path, 0};
            } else if (v.isLambda()) {
                auto pos = state->positions[v.lambda.fun->pos];
                if (auto path = std::get_if<SourcePath>(&pos.origin))
                    return {*path, pos.line};
                else
                    throw Error("'%s' cannot be shown in an editor", pos);
            } else {
                // assume it's a derivation
                return findPackageFilename(*state, v, arg);
            }
        }();

        // Open in EDITOR
        auto args = editorFor(path, line);
        auto editor = args.front();
        args.pop_front();

        // runProgram redirects stdout to a StringSink,
        // using runProgram2 to allow editors to display their UI
        runProgram2(RunOptions { .program = editor, .searchPath = true, .args = args });

        // Reload right after exiting the editor
        state->resetFileCache();
        reloadFiles();
    }

    else if (command == ":t") {
        Value v;
        evalString(arg, v);
        logger->cout(showType(v));
    }

    else if (command == ":u") {
        Value v, f, result;
        evalString(arg, v);
        evalString("drv: (import <nixpkgs> {}).runCommand \"shell\" { buildInputs = [ drv ]; } \"\"", f);
        state->callFunction(f, v, result, PosIdx());

        StorePath drvPath = getDerivationPath(result);
        runNix("nix-shell", {state->store->printStorePath(drvPath)});
    }

    else if (command == ":b" || command == ":bl" || command == ":i" || command == ":sh" || command == ":log") {
        Value v;
        evalString(arg, v);
        StorePath drvPath = getDerivationPath(v);
        Path drvPathRaw = state->store->printStorePath(drvPath);

        if (command == ":b" || command == ":bl") {
            // TODO: this only shows a progress bar for explicitly initiated builds,
            // not eval-time fetching or builds performed for IFD.
            // But we can't just show it everywhere, since that would erase partial output from evaluation.
            startProgressBar();
            Finally stopLogger([&]() {
                stopProgressBar();
            });

            state->store->buildPaths({
                DerivedPath::Built {
                    .drvPath = makeConstantStorePathRef(drvPath),
                    .outputs = OutputsSpec::All { },
                },
            });
            auto drv = state->store->readDerivation(drvPath);
            logger->cout("\nThis derivation produced the following outputs:");
            for (auto & [outputName, outputPath] : state->store->queryDerivationOutputMap(drvPath)) {
                auto localStore = state->store.dynamic_pointer_cast<LocalFSStore>();
                if (localStore && command == ":bl") {
                    std::string symlink = "repl-result-" + outputName;
                    localStore->addPermRoot(outputPath, absPath(symlink));
                    logger->cout("  ./%s -> %s", symlink, state->store->printStorePath(outputPath));
                } else {
                    logger->cout("  %s -> %s", outputName, state->store->printStorePath(outputPath));
                }
            }
        } else if (command == ":i") {
            runNix("nix-env", {"-i", drvPathRaw});
        } else if (command == ":log") {
            settings.readOnlyMode = true;
            Finally roModeReset([&]() {
                settings.readOnlyMode = false;
            });
            auto subs = getDefaultSubstituters();

            subs.push_front(state->store);

            bool foundLog = false;
            RunPager pager;
            for (auto & sub : subs) {
                auto * logSubP = dynamic_cast<LogStore *>(&*sub);
                if (!logSubP) {
                    printInfo("Skipped '%s' which does not support retrieving build logs", sub->getUri());
                    continue;
                }
                auto & logSub = *logSubP;

                auto log = logSub.getBuildLog(drvPath);
                if (log) {
                    printInfo("got build log for '%s' from '%s'", drvPathRaw, logSub.getUri());
                    logger->writeToStdout(*log);
                    foundLog = true;
                    break;
                }
            }
            if (!foundLog) throw Error("build log of '%s' is not available", drvPathRaw);
        } else {
            runNix("nix-shell", {drvPathRaw});
        }
    }

    else if (command == ":p" || command == ":print") {
        Value v;
        evalString(arg, v);
        if (v.type() == nString) {
            std::cout << v.string.s;
        } else {
            printValue(std::cout, v);
        }
        std::cout << std::endl;
    }

    else if (command == ":q" || command == ":quit") {
        state->debugStop = false;
        return ProcessLineResult::Quit;
    }

    else if (command == ":doc") {
        Value v;
        evalString(arg, v);
        if (auto doc = state->getDoc(v)) {
            std::string markdown;

            if (!doc->args.empty() && doc->name) {
                auto args = doc->args;
                for (auto & arg : args)
                    arg = "*" + arg + "*";

                markdown +=
                    "**Synopsis:** `builtins." + (std::string) (*doc->name) + "` "
                    + concatStringsSep(" ", args) + "\n\n";
            }

            markdown += stripIndentation(doc->doc);

            logger->cout(trim(renderMarkdownToTerminal(markdown)));
        } else if (v.isLambda()) {
            auto pos = state->positions[v.lambda.fun->pos];
            if (auto path = std::get_if<SourcePath>(&pos.origin)) {
                // Path and position have now been obtained, feed to nix-doc library to get data.
                auto docComment = lambdaDocsForPos(*path, pos);
                if (!docComment) {
                    throw Error("lambda '%s' has no documentation comment", pos);
                }

                // Build and print Markdown representation of documentation comment.
                std::string markdown = stripIndentation(docComment.get());
                logger->cout(trim(renderMarkdownToTerminal(markdown)));
            } else {
                throw Error("lambda '%s' doesn't have a determinable source file", pos);
            }
        } else {
            throw Error("value '%s' does not have documentation", arg);
        }
    }

    else if (command == ":te" || command == ":trace-enable") {
        if (arg == "false" || (arg == "" && loggerSettings.showTrace)) {
            std::cout << "not showing error traces\n";
            loggerSettings.showTrace = false;
        } else if (arg == "true" || (arg == "" && !loggerSettings.showTrace)) {
            std::cout << "showing error traces\n";
            loggerSettings.showTrace = true;
        } else {
            throw Error("unexpected argument '%s' to %s", arg, command);
        };
    }

    else if (command != "")
        throw Error("unknown command '%1%'", command);

    else {
        size_t p = line.find('=');
        std::string name;
        if (p != std::string::npos &&
            p < line.size() &&
            line[p + 1] != '=' &&
            isVarName(name = removeWhitespace(line.substr(0, p))))
        {
            Expr & e = parseString(line.substr(p + 1));
            Value & v(*state->allocValue());
            v.mkThunk(env, e);
            addVarToScope(state->symbols.create(name), v);
        } else {
            Value v;
            evalString(line, v);
            printValue(std::cout, v, 1);
            std::cout << std::endl;
        }
    }

    return ProcessLineResult::PromptAgain;
}

void NixRepl::loadFile(const Path & path)
{
    loadedFiles.remove(path);
    loadedFiles.push_back(path);
    Value v, v2;
    state->evalFile(lookupFileArg(*state, path), v);
    state->autoCallFunction(*autoArgs, v, v2);
    addAttrsToScope(v2);
}

void NixRepl::loadFlake(const std::string & flakeRefS)
{
    if (flakeRefS.empty())
        throw Error("cannot use ':load-flake' without a path specified. (Use '.' for the current working directory.)");

    auto flakeRef = parseFlakeRef(flakeRefS, absPath("."), true);
    if (evalSettings.pureEval && !flakeRef.input.isLocked())
        throw Error("cannot use ':load-flake' on locked flake reference '%s' (use --impure to override)", flakeRefS);

    Value v;

    flake::callFlake(*state,
        flake::lockFlake(*state, flakeRef,
            flake::LockFlags {
                .updateLockFile = false,
                .useRegistries = !evalSettings.pureEval,
                .allowUnlocked = !evalSettings.pureEval,
            }),
        v);
    addAttrsToScope(v);
}


void NixRepl::initEnv()
{
    env = &state->allocEnv(envSize);
    env->up = &state->baseEnv;
    displ = 0;
    staticEnv->vars.clear();

    varNames.clear();
    for (auto & i : state->staticBaseEnv->vars)
        varNames.emplace(state->symbols[i.first]);
}


void NixRepl::reloadFiles()
{
    initEnv();

    loadFiles();
}


void NixRepl::loadFiles()
{
    Strings old = loadedFiles;
    loadedFiles.clear();

    for (auto & i : old) {
        notice("Loading '%1%'...", Magenta(i));
        loadFile(i);
    }

    for (auto & [i, what] : getValues()) {
        notice("Loading installable '%1%'...", Magenta(what));
        addAttrsToScope(*i);
    }

    loadReplOverlays();
}

void NixRepl::loadReplOverlays()
{
    if (!evalSettings.replOverlays) {
        return;
    }

    notice("Loading '%1%'...", Magenta("repl-overlays"));
    auto replInitFilesFunction = getReplOverlaysEvalFunction();

    Value &newAttrs(*state->allocValue());
    SmallValueVector<3> args = {replInitInfo(), bindingsToAttrs(), replOverlays()};
    state->callFunction(
        *replInitFilesFunction,
        args.size(),
        args.data(),
        newAttrs,
        replInitFilesFunction->determinePos(noPos)
    );

    // n.b. this does in fact load the stuff into the environment twice (once
    // from the superset of the environment returned by repl-overlays and once
    // from the thing itself), but it's not fixable because clearEnv here could
    // lead to dangling references to the old environment in thunks.
    // https://git.lix.systems/lix-project/lix/issues/337#issuecomment-3745
    addAttrsToScope(newAttrs);
}

Value * NixRepl::getReplOverlaysEvalFunction()
{
    if (replOverlaysEvalFunction && *replOverlaysEvalFunction) {
        return *replOverlaysEvalFunction;
    }

    auto evalReplInitFilesPath = CanonPath::root + "repl-overlays.nix";
    *replOverlaysEvalFunction = state->allocValue();
    auto code =
        #include "repl-overlays.nix.gen.hh"
        ;
    auto & expr = state->parseExprFromString(
        code,
        SourcePath(evalReplInitFilesPath),
        state->staticBaseEnv
    );

    state->eval(expr, **replOverlaysEvalFunction);

    return *replOverlaysEvalFunction;
}

Value * NixRepl::replOverlays()
{
    Value * replInits(state->allocValue());
    state->mkList(*replInits, evalSettings.replOverlays.get().size());
    Value ** replInitElems = replInits->listElems();

    size_t i = 0;
    for (auto path : evalSettings.replOverlays.get()) {
        debug("Loading '%1%' path '%2%'...", "repl-overlays", path);
        SourcePath sourcePath((CanonPath(path)));
        auto replInit = evalFile(sourcePath);

        if (!replInit->isLambda()) {
            state->error<TypeError>(
                "Expected `repl-overlays` to be a lambda but found %1%: %2%",
                showType(*replInit),
                ValuePrinter(*state, *replInit, errorPrintOptions)
            )
            .atPos(replInit->determinePos(noPos))
            .debugThrow();
        }

        if (replInit->lambda.fun->hasFormals()
            && !replInit->lambda.fun->formals->ellipsis) {
            state->error<TypeError>(
                "Expected first argument of %1% to have %2% to allow future versions of Lix to add additional attributes to the argument",
                "repl-overlays",
                "..."
            )
                .atPos(replInit->determinePos(noPos))
                .debugThrow();
        }

        replInitElems[i] = replInit;
        i++;
    }


    return replInits;
}

Value * NixRepl::replInitInfo()
{
    auto builder = state->buildBindings(2);

    Value * currentSystem(state->allocValue());
    currentSystem->mkString(evalSettings.getCurrentSystem());
    builder.insert(state->symbols.create("currentSystem"), currentSystem);

    Value * info(state->allocValue());
    info->mkAttrs(builder.finish());
    return info;
}


void NixRepl::addAttrsToScope(Value & attrs)
{
    state->forceAttrs(attrs, [&]() { return attrs.determinePos(noPos); }, "while evaluating an attribute set to be merged in the global scope");
    if (displ + attrs.attrs->size() >= envSize)
        throw Error("environment full; cannot add more variables");

    for (auto & i : *attrs.attrs) {
        staticEnv->vars.emplace_back(i.name, displ);
        env->values[displ++] = i.value;
        varNames.emplace(state->symbols[i.name]);
    }
    staticEnv->sort();
    staticEnv->deduplicate();
    notice("Added %1% variables.", attrs.attrs->size());
}


void NixRepl::addVarToScope(const Symbol name, Value & v)
{
    if (displ >= envSize)
        throw Error("environment full; cannot add more variables");
    if (auto oldVar = staticEnv->find(name); oldVar != staticEnv->vars.end())
        staticEnv->vars.erase(oldVar);
    staticEnv->vars.emplace_back(name, displ);
    staticEnv->sort();
    env->values[displ++] = &v;
    varNames.emplace(state->symbols[name]);
}

Value * NixRepl::bindingsToAttrs()
{
    auto builder = state->buildBindings(staticEnv->vars.size());
    for (auto & [symbol, displacement] : staticEnv->vars) {
        builder.insert(symbol, env->values[displacement]);
    }

    Value * attrs(state->allocValue());
    attrs->mkAttrs(builder.finish());
    return attrs;
}


Expr & NixRepl::parseString(std::string s)
{
    return state->parseExprFromString(std::move(s), state->rootPath(CanonPath::fromCwd()), staticEnv);
}


void NixRepl::evalString(std::string s, Value & v)
{
    Expr & e = parseString(s);
    e.eval(*state, *env, v);
    state->forceValue(v, v.determinePos(noPos));
}

Value * NixRepl::evalFile(SourcePath & path)
{
    auto & expr = state->parseExprFromFile(path, staticEnv);
    Value * result(state->allocValue());
    expr.eval(*state, *env, *result);
    state->forceValue(*result, result->determinePos(noPos));
    return result;
}


std::unique_ptr<AbstractNixRepl> AbstractNixRepl::create(
   const SearchPath & searchPath, nix::ref<Store> store, ref<EvalState> state,
   std::function<AnnotatedValues()> getValues)
{
    return std::make_unique<NixRepl>(
        searchPath,
        openStore(),
        state,
        getValues
    );
}


ReplExitStatus AbstractNixRepl::runSimple(
    ref<EvalState> evalState,
    const ValMap & extraEnv)
{
    auto getValues = [&]()->NixRepl::AnnotatedValues{
        NixRepl::AnnotatedValues values;
        return values;
    };
    SearchPath searchPath = {};
    auto repl = std::make_unique<NixRepl>(
            searchPath,
            openStore(),
            evalState,
            getValues
        );

    repl->initEnv();

    // add 'extra' vars.
    for (auto & [name, value] : extraEnv)
        repl->addVarToScope(repl->state->symbols.create(name), *value);

    return repl->mainLoop();
}

}
