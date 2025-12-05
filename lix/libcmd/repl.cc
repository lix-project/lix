#include <cstdio>
#include <editline.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "lix/libexpr/value.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libcmd/repl-interacter.hh"
#include "lix/libcmd/repl.hh"

#include "lix/libutil/ansicolor.hh"
#include "lix/libmain/shared.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/log-store.hh"
#include "lix/libcmd/common-eval-args.hh"
#include "lix/libexpr/get-drvs.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/globals.hh"
#include "lix/libexpr/flake/flake.hh"
#include "lix/libexpr/flake/lockfile.hh"
#include "lix/libcmd/editor-for.hh"
#include "lix/libutil/finally.hh"
#include "lix/libcmd/markdown.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libutil/signals.hh"
#include "lix/libexpr/print.hh"
#include "lix/libexpr/gc-small-vector.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/users.hh"

#if HAVE_BOEHMGC
#define GC_INCLUDE_NEW
#include <gc/gc_cpp.h>
#endif

// XXX: These are for lix-doc features and will be removed in a future rewrite where this functionality is integrated more natively.
extern "C" {
    char const *lixdoc_get_function_docs(char const *filename, size_t line, size_t col);
    void lixdoc_free_string(char const *str);
}

namespace nix {


/** Wrapper around std::unique_ptr with a custom deleter for strings from nix-doc **/
using NdString = std::unique_ptr<const char, decltype(&lixdoc_free_string)>;

/**
 * Fetch a string representing the doc comment using nix-doc and wrap it in an RAII wrapper.
 */
NdString lambdaDocsForPos(SourcePath const path, nix::Pos const &pos) {
  std::string const file = path.to_string();
  // NOLINTNEXTLINE(lix-unsafe-c-calls): paths are safe
  return NdString{lixdoc_get_function_docs(file.c_str(), pos.line, pos.column), &lixdoc_free_string};
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

enum class ReplLoadKind
{
    File,
    Flake,
};

// std::variant or virtual inheritence would both be overkill for this.
struct ReplLoadable
{
    std::string spec;
    ReplLoadKind kind;

    friend constexpr auto operator<=>(ReplLoadable const &, ReplLoadable const &) = default;
};

struct NixRepl;
using ReplFunction = std::function<ProcessLineResult(NixRepl &, const std::string &)>;
using PrintDerivationOutputFunction =
    std::function<std::string(const std::string &, const StorePath &)>;

struct CommandArgumentSpecifier
{
    std::string placeholderText;
    bool optional = false;
};

static const CommandArgumentSpecifier argExpr = {.placeholderText = "expr"};
static const CommandArgumentSpecifier argPath = {.placeholderText = "path"};

struct CommandAttributes
{
    std::list<std::string> aliases;
    /**
     * Whether this command can only be used inside of the debugger.
     */
    bool debugModeOnly;
    std::optional<std::string> help;
    std::optional<std::string> section;
    std::list<CommandArgumentSpecifier> positionalArgsSpecifiers;
};

struct REPLCommand
{
    ReplFunction handler;
    CommandAttributes attributes;
};

struct UnexpectedArgument : Error
{
    std::string argValue;

    UnexpectedArgument(const std::string & argValue)
        : Error("unexpected argument")
        , argValue(argValue)
    {
    }
};

struct NixRepl
    : AbstractNixRepl
    , detail::ReplCompleterMixin
    #if HAVE_BOEHMGC
    , gc
    #endif
{
    Evaluator & evaluator;
    size_t debugTraceIndex;

    std::list<ReplLoadable> loaded;
    std::function<AnnotatedValues()> getValues;
    std::map<std::string, std::shared_ptr<REPLCommand>> registeredCommands;

    // Uses 8MiB of memory. It's fine.
    const static int envSize = 1 << 20;
    std::shared_ptr<StaticEnv> staticEnv;
    Env * env;
    int displ;
    StringSet varNames;

    box_ptr<ReplInteracter> interacter;

    NixRepl(const SearchPath & searchPath, nix::ref<Store> store, EvalState & state,
            std::function<AnnotatedValues()> getValues);
    virtual ~NixRepl() = default;

    ReplExitStatus mainLoop() override;
    void initEnv() override;

    void initDebugBuiltinCommands();
    void initBuiltinCommands();

    virtual StringSet completePrefix(const std::string & prefix) override;

    /**
     * @exception nix::Error thrown directly if the expression does not evaluate
     * to a derivation, or evaluates to an invalid derivation.
     */
    StorePath getDerivationPath(Value & v);
    /**
     * Evaluate a string argument into a store path.
     */
    StorePath evalIntoDerivationPath(const std::string & drvArg);
    /**
     * Build a derivation path and show a progress bar for it.
     */
    Derivation buildWithProgressBar(const StorePath & drvPath);
    /**
     * Print the derivation outputs produced by a derivation.
     * A function that should return the string for each output can be passed
     * to customize the formatting and perform additional actions such as adding permanent roots.
     */
    void printDerivationOutputs(const StorePath & drvPath, PrintDerivationOutputFunction printFn);

    ProcessLineResult processLine(std::string line);

    bool inDebugger() const
    {
        return evaluator.debug && evaluator.debug->inDebugger;
    }

    void loadFile(const Path & path);
    void loadFlake(const std::string & flakeRef);
    void loadFiles();
    void reloadFiles();

    void addCommand(
        const std::string & name,
        ReplFunction && function,
        const CommandAttributes & attributes = {}
    );
    void generateHelpCommand();

    template<typename T, typename NameFn, typename ValueFn>
    void addToScope(T && things, NameFn nameFn, ValueFn valueFn);

    void addAttrsToScope(Value & attrs);
    void addValMapToScope(const ValMap & attrs);
    void addVarToScope(const Symbol name, Value & v);
    Expr & parseString(std::string s);
    std::variant<std::unique_ptr<Expr>, ExprReplBindings> parseReplString(std::string s);
    void evalString(std::string s, Value & v);
    void loadDebugTraceEnv(const DebugTrace & dt);

    /**
     * Load the `repl-overlays` and add the resulting AttrSet to the top-level
     * bindings.
     */
    void loadReplOverlays();

    /**
     * Get a list of each of the `repl-overlays` (parsed and evaluated).
     */
    Value replOverlays();

    /**
     * Get the Nix function that composes the `repl-overlays` together.
     */
    Value getReplOverlaysEvalFunction();

    /**
     * Cached return value of `getReplOverlaysEvalFunction`.
     *
     * Note: This is `shared_ptr` to avoid garbage collection.
     */
    std::shared_ptr<std::optional<Value>> replOverlaysEvalFunction =
        std::allocate_shared<std::optional<Value>>(
            TraceableAllocator<std::optional<Value>>(), std::nullopt
        );

    /**
     * Get the `info` AttrSet that's passed as the first argument to each
     * of the `repl-overlays`.
     */
    Value replInitInfo();

    /**
     * Get the current top-level bindings as an AttrSet.
     */
    Value bindingsToAttrs();
    /**
     * Parse a file, evaluate its result, and force the resulting value.
     */
    Value evalFile(SourcePath & path);

    void printValue(std::ostream & str,
                              Value & v,
                              unsigned int maxDepth = std::numeric_limits<unsigned int>::max(),
                              unsigned int derivationPathDepth = 0)
    {
        ::nix::printValue(state, str, v, PrintOptions {
            .ansiColors = true,
            .force = true,
            .derivationPathDepth = derivationPathDepth,
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

NixRepl::NixRepl(const SearchPath & searchPath, nix::ref<Store> store, EvalState & state,
            std::function<NixRepl::AnnotatedValues()> getValues)
    : AbstractNixRepl(state)
    , evaluator(state.ctx)
    , debugTraceIndex(0)
    , getValues(getValues)
    , staticEnv(new StaticEnv(nullptr, evaluator.builtins.staticEnv.get()))
    , interacter(makeInteracter())
{
    initBuiltinCommands();
}

void runNix(Path program, const Strings & args)
{
    auto subprocessEnv = getEnv();
    subprocessEnv["NIX_CONFIG"] = globalConfig.toKeyValue(true);

    runProgram2(RunOptions {
        .program = settings.nixBinDir+ "/" + program,
        .args = args,
        .environment = subprocessEnv,
    }).waitAndCheck();

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
        if (inDebugger()) {
            debuggerNotice = " debugger";
        }
        notice("Lix %1%%2%\nType :? for help.", Uncolored(nixVersion), debuggerNotice);
    }

    isFirstRepl = false;

    loadFiles();

    auto _guard = interacter->init(static_cast<detail::ReplCompleterMixin *>(this));

    /* Stop the progress bar because it interferes with the display of
       the repl. */
    logger->pause();

    std::string input;

    while (true) {
        unsetUserInterruptRequest();

        // When continuing input from previous lines, don't print a prompt, just align to the same
        // number of chars as the prompt.
        if (!interacter->getLine(input, input.empty() ? ReplPromptType::ReplPrompt : ReplPromptType::ContinuationPrompt)) {
            // Ctrl-D should exit the debugger.
            if (evaluator.debug) {
                evaluator.debug->stop = false;
            }
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
                printMsg(lvlError, "%1%", Uncolored(e.msg()));
            }
        } catch (EvalError & e) {
            printMsg(lvlError, "%1%", Uncolored(e.msg()));
        } catch (Error & e) {
            printMsg(lvlError, "%1%", Uncolored(e.msg()));
        } catch (Interrupted & e) {
            printMsg(lvlError, "%1%", Uncolored(e.msg()));
        }

        // We handled the current input fully, so we should clear it
        // and read brand new input.
        input.clear();
        std::cout << std::endl;
    }
}

StringSet NixRepl::completePrefix(const std::string &prefix)
{
    StringSet completions;

    // We should only complete colon commands if there's a colon at the beginning,
    // but editline (for... whatever reason) doesn't *give* us the colon in the
    // completion callback. If the user types :rel<TAB>, `prefix` will only be `rel`.
    // Luckily, editline provides a global variable for its current buffer, so we can
    // check for the presence of a colon there.
    if (rl_line_buffer != nullptr && rl_line_buffer[0] == ':') {
        for (auto const & [colonCmd, cmd] : registeredCommands) {
            if ((!cmd->attributes.debugModeOnly || inDebugger()) && colonCmd.starts_with(prefix)) {
                completions.insert(std::string(colonCmd));
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
        // To handle cases like `foo."bar.`, walk back the cursor
        // to the previous dot if there are an odd number of quotes.
        auto quoteCount =
            std::count_if(cur.begin(), cur.begin() + dot, [](char c) { return c == '"'; });
        if (quoteCount % 2 != 0) {
            // Find the last quote before the dot
            auto prevQuote = cur.rfind('"', dot - 1);
            if (prevQuote != std::string::npos) {
                // And the previous dot prior to that quote
                auto prevDot = cur.rfind('.', prevQuote);
                if (prevDot != std::string::npos) {
                    dot = prevDot;
                }
            }
        }

        /* Temporarily disable the debugger, to avoid re-entering readline. */
        auto debug = std::move(evaluator.debug);
        Finally restoreDebug([&]() { evaluator.debug = std::move(debug); });
        try {
            /* This is an expression that should evaluate to an
               attribute set.  Evaluate it to get the names of the
               attributes. */
            auto expr = cur.substr(0, dot);
            auto cur2 = cur.substr(dot + 1);

            Expr & e = parseString(expr);
            Value v;
            e.eval(state, *env, v);
            state.forceAttrs(v, noPos, "while evaluating an attrset for the purpose of completion (this error should not be displayed; file an issue?)");

            for (auto & i : *v.attrs()) {
                std::ostringstream output;
                printAttributeName(output, evaluator.symbols[i.name]);
                std::string name = output.str();

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

StorePath NixRepl::getDerivationPath(Value & v) {
    auto drvInfo = getDerivation(state, v, false);
    if (!drvInfo)
        throw Error("expression does not evaluate to a derivation, so I can't build it");
    auto drvPath = drvInfo->queryDrvPath(state);
    if (!drvPath)
        throw Error("expression did not evaluate to a valid derivation (no 'drvPath' attribute)");
    if (!state.aio.blockOn(evaluator.store->isValidPath(*drvPath)))
        throw Error("expression evaluated to invalid derivation '%s'", evaluator.store->printStorePath(*drvPath));
    return *drvPath;
}

StorePath NixRepl::evalIntoDerivationPath(const std::string & drvArg)
{
    Value v;
    evalString(drvArg, v);

    return getDerivationPath(v);
}

Derivation NixRepl::buildWithProgressBar(const StorePath & drvPath)
{
    // TODO: this only shows a progress bar for explicitly initiated builds,
    // not eval-time fetching or builds performed for IFD.
    // But we can't just show it everywhere, since that would erase partial output from evaluation.
    logger->resetProgress();
    logger->resume();
    Finally stopLogger([&]() { logger->pause(); });
    state.aio.blockOn(evaluator.store->buildPaths({
        DerivedPath::Built{
            .drvPath = makeConstantStorePath(drvPath),
            .outputs = OutputsSpec::All{},
        },
    }));
    auto drv = state.aio.blockOn(evaluator.store->readDerivation(drvPath));

    return drv;
}

void NixRepl::printDerivationOutputs(
    const StorePath & drvPath, PrintDerivationOutputFunction printFn
)
{
    logger->cout("\nThis derivation produced the following outputs:");
    for (auto & [outputName, outputPath] :
         state.aio.blockOn(evaluator.store->queryDerivationOutputMap(drvPath)))
    {
        logger->cout(printFn(outputName, outputPath));
    }
}

void NixRepl::loadDebugTraceEnv(const DebugTrace & dt)
{
    initEnv();

    auto se = evaluator.debug->staticEnvFor(dt.expr);
    if (se) {
        auto vm = mapStaticEnvBindings(evaluator.symbols, *se.get(), dt.env);

        // add staticenv vars.
        addValMapToScope(*vm);
    }
}

void NixRepl::addCommand(
    const std::string & name, ReplFunction && handler, const CommandAttributes & attributes
)
{
    if (registeredCommands.contains(name)) {
        throw Error("Command '%s' is already registered: commands cannot be shadowed", name);
    }

    registeredCommands[name] = std::make_shared<REPLCommand>(std::move(handler), attributes);

    for (auto & alias : attributes.aliases) {
        if (registeredCommands.contains(alias)) {
            throw Error(
                "Command '%s' is already registered: alias (original command '%s') cannot shadow other "
                "commands",
                name,
                alias
            );
        }

        registeredCommands[alias] = registeredCommands[name];
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
        command = line.substr(1, p - 1);
        if (p != std::string::npos) arg = removeWhitespace(line.substr(p));
    } else {
        arg = line;
    }

    if (registeredCommands.contains(command)) {
        auto registeredCommand = registeredCommands[command];
        try {
            if (registeredCommand->attributes.debugModeOnly && !inDebugger()) {
                throw Error("command '%s' can only be used when the debugger is active", command);
            }

            return registeredCommand->handler(*this, arg);
        } catch (UnexpectedArgument & excArg) {
            throw Error("unexpected argument '%1%' to command '%2%", excArg.argValue, command);
        }
    } else if (command != "") {
        throw Error("unknown command '%1%'", command);
    } else {
        /* A line is either a regular expression or a `var = expr` assignment */
        std::variant<std::unique_ptr<Expr>, ExprReplBindings> result = parseReplString(line);
        std::visit(
            overloaded{
                [&](ExprReplBindings & b) {
                    for (auto & [name, e] : b.symbols) {
                        Value v;
                        e->eval(state, *env, v);
                        // NONEXTLINE(bugprone-unused-return-value): leak because of thunk
                        // references
                        (void) e.release();
                        addVarToScope(name, v);
                    }
                },
                [&](std::unique_ptr<Expr> & e) {
                    Value v;
                    e->eval(state, *env, v);
                    // NONEXTLINE(bugprone-unused-return-value): leak because of thunk references
                    (void) e.release();
                    state.forceValue(v, noPos);
                    printValue(std::cout, v, 1);
                    std::cout << std::endl;
                }
            },
            result
        );
    }

    return ProcessLineResult::PromptAgain;
}

void NixRepl::initDebugBuiltinCommands()
{
    addCommand(
        "backtrace",
        [](NixRepl & repl, const std::string & _arg) {
            auto traces = repl.evaluator.debug->traces();
            for (const auto & [idx, i] : enumerate(traces)) {
                std::cout << "\n" << ANSI_BLUE << idx << ANSI_NORMAL << ": ";
                showDebugTrace(std::cout, repl.evaluator.positions, *i);
            }
            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"bt"},
         .debugModeOnly = true,
         .help = "Show trace stack",
         .section = "Debug mode"}
    );

    addCommand(
        "show-trace",
        [](NixRepl & repl, const std::string & arg) {
            try {
                repl.debugTraceIndex = stoi(arg);
            } catch (...) {
            }

            auto traces = repl.evaluator.debug->traces();
            for (const auto & [idx, i] : enumerate(traces)) {
                if (idx == repl.debugTraceIndex) {
                    std::cout << "\n" << ANSI_BLUE << idx << ANSI_NORMAL << ": ";
                    showDebugTrace(std::cout, repl.evaluator.positions, *i);
                    std::cout << std::endl;
                    printEnvBindings(repl.state, i->expr, i->env);
                    repl.loadDebugTraceEnv(*i);
                    break;
                }
            }
            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"st"},
         .debugModeOnly = true,
         .help = "Show current trace. If an integer is provided, this switches to that stack "
                 "beforehand.",
         .section = "Debug mode",
         .positionalArgsSpecifiers = {{.placeholderText = "integer index", .optional = true}}}
    );

    addCommand(
        "step",
        [](NixRepl & repl, const std::string & _arg) {
            repl.evaluator.debug->stop = true;
            return ProcessLineResult::Continue;
        },
        {.aliases = {"s"}, .debugModeOnly = true, .help = "Go one step", .section = "Debug mode"}
    );

    addCommand(
        "continue",
        [](NixRepl & repl, const std::string & _arg) {
            repl.evaluator.debug->stop = false;
            return ProcessLineResult::Continue;
        },
        {.aliases = {"c"},
         .debugModeOnly = true,
         .help = "Go until end of program, exception or builtins.break",
         .section = "Debug mode"}
    );
}

void NixRepl::initBuiltinCommands()
{
    initDebugBuiltinCommands();

    addCommand(
        "add",
        [](NixRepl & repl, const std::string & arg) {
            Value v;
            repl.evalString(arg, v);
            repl.addAttrsToScope(v);

            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"a"},
         .help = "Add attributes from resulting set to scope",
         .positionalArgsSpecifiers = {argExpr}}
    );

    addCommand(
        "load",
        [](NixRepl & repl, const std::string & arg) {
            repl.state.resetFileCache();
            repl.loadFile(arg);

            return ProcessLineResult::PromptAgain;
        },
        {
            .aliases = {"l"},
            .help = "Load Nix expression and add it to scope",
            .positionalArgsSpecifiers = {argPath},
        }
    );

    addCommand(
        "reload",
        [](NixRepl & repl, const std::string & _arg) {
            repl.state.resetFileCache();
            repl.reloadFiles();

            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"r"}, .help = "Reload all files successfully loaded"}
    );

    addCommand(
        "edit",
        [](NixRepl & repl, const std::string & arg) {
            Value v;
            repl.evalString(arg, v);

            const auto [path, line] = [&]() -> std::pair<SourcePath, uint32_t> {
                if (v.type() == nPath || v.type() == nString) {
                    NixStringContext context;
                    auto path = repl.state.coerceToPath(
                        noPos, v, context, "while evaluating the filename to edit"
                    );
                    return {path, 0};
                } else if (v.isLambda()) {
                    auto pos = repl.evaluator.positions[v.lambda().fun->pos];
                    if (auto path = std::get_if<CheckedSourcePath>(&pos.origin)) {
                        return {*path, pos.line};
                    }
                    throw Error("'%s' cannot be shown in an editor", pos);
                } else {
                    return findPackageFilename(repl.state, v, arg);
                }
            }();

            auto args = editorFor(path, line);
            auto editor = args.front();
            args.pop_front();

            runProgram2(RunOptions{.program = editor, .searchPath = true, .args = args})
                .waitAndCheck();

            if (!repl.evaluator.store->isInStore(canonPath(path.canonical().abs(), true))) {
                repl.state.resetFileCache();
                repl.reloadFiles();
            }

            return ProcessLineResult::PromptAgain;
        },
        {
            .aliases = {"e"},
            .help = "Open package or function in $EDITOR",
            .positionalArgsSpecifiers = {argExpr},
        }
    );

    addCommand(
        "type",
        [](NixRepl & repl, const std::string & arg) {
            Value v;
            repl.evalString(arg, v);
            logger->cout(showType(v));

            return ProcessLineResult::PromptAgain;
        },
        {
            .aliases = {"t"},
            .help = "Describe result of evaluation",
            .positionalArgsSpecifiers = {argExpr},
        }
    );

    addCommand(
        "use",
        [](NixRepl & repl, const std::string & arg) {
            Value v, f, result;
            repl.evalString(arg, v);
            repl.evalString(
                R""("drv: (import <nixpkgs> {}).runCommand "shell")""
                R""({ buildInputs = [ drv ]; } "")"",
                f
            );
            repl.state.callFunction(f, v, result, PosIdx());

            StorePath drvPath = repl.getDerivationPath(result);
            runNix("nix-shell", {repl.evaluator.store->printStorePath(drvPath)});

            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"u"},
         .help = "Build derivation, then start nix-shell",
         .positionalArgsSpecifiers = {argExpr}}
    );

    addCommand(
        "log",
        [](NixRepl & repl, const std::string & arg) {
            if (arg.empty()) {
                throw Error("cannot use ':log' without specifying a derivation");
            }

            StorePath drvPath = ([&] {
                auto maybeDrvPath = repl.evaluator.store->maybeParseStorePath(arg);
                if (maybeDrvPath && maybeDrvPath->isDerivation()) {
                    return std::move(*maybeDrvPath);
                } else {
                    Value v;
                    repl.evalString(arg, v);
                    return repl.getDerivationPath(v);
                }
            })();
            Path drvPathRaw = repl.evaluator.store->printStorePath(drvPath);

            settings.readOnlyMode = true;
            Finally roReset([&]() { settings.readOnlyMode = false; });

            auto subs = repl.state.aio.blockOn(getDefaultSubstituters());
            subs.push_front(repl.evaluator.store);

            bool foundLog = false;
            RunPager pager;

            for (auto & sub : subs) {
                auto * logSubP = dynamic_cast<LogStore *>(&*sub);
                if (!logSubP) {
                    printInfo(
                        "Skipped '%s' which does not support retrieving build logs", sub->getUri()
                    );
                    continue;
                }
                auto & logSub = *logSubP;

                auto log = repl.state.aio.blockOn(logSub.getBuildLog(drvPath));
                if (log) {
                    printInfo("got build log for '%s' from '%s'", drvPathRaw, logSub.getUri());
                    logger->writeToStdout(*log);
                    foundLog = true;
                    break;
                }
            }

            if (!foundLog) {
                throw Error("build log of '%s' is not available", drvPathRaw);
            }

            return ProcessLineResult::PromptAgain;
        },
        {.help = "Show logs for a derivation",
         .positionalArgsSpecifiers = {{.placeholderText = "expr | .drv path"}}}
    );

    addCommand(
        "build",
        [](NixRepl & repl, const std::string & arg) {
            auto drvPath = repl.evalIntoDerivationPath(arg);
            auto drv = repl.buildWithProgressBar(drvPath);

            repl.printDerivationOutputs(
                drvPath, [&repl](const std::string & outputName, const StorePath & outputPath) {
                    return fmt(
                        "  %s -> %s", outputName, repl.evaluator.store->printStorePath(outputPath)
                    );
                }
            );

            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"b"}, .help = "Build a derivation", .positionalArgsSpecifiers = {argExpr}}
    );

    addCommand(
        "build-with-gc-roots",
        [](NixRepl & repl, const std::string & arg) {
            auto drvPath = repl.evalIntoDerivationPath(arg);
            auto drv = repl.buildWithProgressBar(drvPath);

            repl.printDerivationOutputs(
                drvPath, [&repl](const std::string & outputName, const StorePath & outputPath) {
                    auto localStore = repl.evaluator.store.try_cast_shared<LocalFSStore>();

                    std::string symlink = fmt("repl-result-%s", outputName);
                    repl.state.aio.blockOn(localStore->addPermRoot(outputPath, absPath(symlink)));
                    return fmt(
                        "  ./%s -> %s", symlink, repl.evaluator.store->printStorePath(outputPath)
                    );
                }
            );

            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"bl"},
         .help = "Build a derivation, creating GC roots in the working directory",
         .positionalArgsSpecifiers = {argExpr}}
    );

    addCommand(
        "build-and-install",
        [](NixRepl & repl, const std::string & arg) {
            Path drvPathRaw =
                repl.evaluator.store->printStorePath(repl.evalIntoDerivationPath(arg));

            runNix("nix-env", {"-i", drvPathRaw});

            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"i"},
         .help = "Build derivation, then install result into current profile",
         .positionalArgsSpecifiers = {argExpr}}
    );

    addCommand(
        "shell",
        [](NixRepl & repl, const std::string & arg) {
            Path drvPathRaw =
                repl.evaluator.store->printStorePath(repl.evalIntoDerivationPath(arg));

            runNix("nix-shell", {drvPathRaw});

            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"sh"},
         .help = "Build dependencies of derivation, then start nix-shell",
         .positionalArgsSpecifiers = {argExpr}}
    );

    addCommand(
        "print",
        [](NixRepl & repl, const std::string & arg) {
            Value v;
            repl.evalString(arg, v);
            if (v.type() == nString) {
                std::cout << v.str();
            } else if (v.type() == nAttrs && repl.state.isDerivation(v)) {
                repl.printValue(std::cout, v, 2, 1);
            } else {
                repl.printValue(std::cout, v, std::numeric_limits<unsigned int>::max(), 0);
            }
            std::cout << std::endl;

            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"p"},
         .help = "Evaluate and print expression recursively\n"
                 "Strings are printed directly, without escaping.",
         .positionalArgsSpecifiers = {argExpr}}
    );

    addCommand(
        "quit",
        [](NixRepl & repl, const std::string & _arg) {
            if (repl.evaluator.debug) {
                repl.evaluator.debug->stop = false;
            }
            return ProcessLineResult::Quit;
        },
        {.aliases = {"q"}, .help = "Exit the REPL"}
    );

    addCommand(
        "doc",
        [](NixRepl & repl, const std::string & arg) {
            Value v;
            repl.evalString(arg, v);

            if (auto doc = repl.evaluator.builtins.getDoc(v)) {
                std::string markdown;

                if (!doc->args.empty() && doc->name) {
                    auto args = doc->args;
                    for (auto & arg : args) {
                        arg = "*" + arg + "*";
                    }

                    markdown += "**Synopsis:** `builtins." + (std::string) *doc->name + "` "
                        + concatStringsSep(" ", args) + "\n\n";
                }

                markdown += stripIndentation(doc->doc);
                logger->cout(trim(renderMarkdownToTerminal(markdown)));

            } else if (v.isLambda()) {
                auto pos = repl.evaluator.positions[v.lambda().fun->pos];
                if (auto path = std::get_if<CheckedSourcePath>(&pos.origin)) {
                    auto docComment = lambdaDocsForPos(*path, pos);
                    if (!docComment) {
                        throw Error("lambda '%s' has no documentation comment", pos);
                    }

                    std::string markdown = stripIndentation(docComment.get());
                    logger->cout(trim(renderMarkdownToTerminal(markdown)));
                } else {
                    throw Error("lambda '%s' doesn't have a determinable source file", pos);
                }
            } else {
                throw Error("value '%s' does not have documentation", arg);
            }

            return ProcessLineResult::PromptAgain;
        },
        {.help = "Show documentation for the provided function (experimental lambda support)",
         .positionalArgsSpecifiers = {argExpr}}
    );

    addCommand(
        "trace-enable",
        [](NixRepl & repl, const std::string & arg) {
            if (arg == "false" || (arg == "" && loggerSettings.showTrace)) {
                std::cout << "not showing error traces\n";
                loggerSettings.showTrace.override(false);
            } else if (arg == "true" || (arg == "" && !loggerSettings.showTrace)) {
                std::cout << "showing error traces\n";
                loggerSettings.showTrace.override(true);
            } else {
                throw UnexpectedArgument(arg);
            }

            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"te"},
         .help = "Enable, disable, or toggle showing traces for errors",
         .positionalArgsSpecifiers = {{.placeholderText = "bool", .optional = true}}}
    );

    addCommand(
        "env",
        [](NixRepl & repl, const std::string & _arg) {
            if (repl.inDebugger()) {
                auto traces = repl.evaluator.debug->traces();
                for (const auto & [idx, i] : enumerate(traces)) {
                    if (idx == repl.debugTraceIndex) {
                        printEnvBindings(repl.state, i->expr, i->env);
                        break;
                    }
                }
            } else {
                printEnvBindings(repl.state.ctx.symbols, *repl.staticEnv, *repl.env, 0);
            }

            return ProcessLineResult::PromptAgain;
        },
        {.help = "Show environment stack"}
    );

    addCommand(
        "load-flake",
        [](NixRepl & repl, const std::string & arg) {
            repl.loadFlake(arg);

            return ProcessLineResult::PromptAgain;
        },
        {.aliases = {"lf"},
         .help = "Load Nix flake and add it to the scope",
         .section = "Flakes",
         .positionalArgsSpecifiers = {{.placeholderText = "flakeref"}}}
    );

    generateHelpCommand();
}

static Strings wrapText(const std::string & text, size_t width)
{
    Strings lines, inputLines;

    inputLines = tokenizeString<Strings>(text, "\n");

    for (auto & line : inputLines) {
        // Preserve empty lines
        if (line.empty()) {
            lines.emplace_back("");
            continue;
        }

        std::string wrappedLine;
        Strings words = tokenizeString<Strings>(line, " ");

        for (auto & word : words) {
            if (wrappedLine.size() + word.size() + 1 > width) {
                lines.push_back(wrappedLine);
                wrappedLine.clear();
            }
            if (!wrappedLine.empty()) {
                wrappedLine += " ";
            }
            wrappedLine += word;
        }

        if (!wrappedLine.empty()) {
            lines.push_back(wrappedLine);
        }
    }

    return lines;
}

void NixRepl::generateHelpCommand()
{
    addCommand(
        "help",
        [](NixRepl & repl, const std::string & _arg) {
            // Special entries appear at the top, general entries stems from command registration.
            std::map<std::string, std::string> generalEntries, specialEntries;
            std::map<std::string, std::map<std::string, std::string>> perSectionEntries;
            std::cout << "The following commands are available:\n"
                      << "\n";

            specialEntries["<expr>"] = "Evaluate and print expression";
            specialEntries["<x> = <expr>"] = "Bind expression to variable";

            size_t maxLhsWidth = 20;
            for (auto & [name, command] : repl.registeredCommands) {
                auto aliases = command->attributes.aliases;
                bool isAlias = std::find(aliases.begin(), aliases.end(), name) != aliases.end();

                if (command->attributes.debugModeOnly && !repl.inDebugger()) {
                    continue;
                }

                if (isAlias) {
                    continue;
                }

                auto lhs = concatMapStringsSep(", ", aliases, [](const std::string & alias) {
                    return ":" + alias;
                });
                if (!lhs.empty()) {
                    lhs += ", ";
                }

                lhs += ":";
                lhs += name;

                if (!command->attributes.positionalArgsSpecifiers.empty()) {
                    lhs += " ";
                }

                lhs += concatMapStringsSep(
                    " ",
                    command->attributes.positionalArgsSpecifiers,
                    [](const CommandArgumentSpecifier & specifier) {
                        if (specifier.optional) {
                            return "[" + specifier.placeholderText + "]";
                        } else {
                            return "<" + specifier.placeholderText + ">";
                        }
                    }
                );

                maxLhsWidth = std::max(maxLhsWidth, lhs.size() + 5);
                auto helpText =
                    command->attributes.help.value_or("No help text is provided for this command.");

                if (!command->attributes.section) {
                    generalEntries[lhs] = helpText;
                } else {
                    perSectionEntries[*command->attributes.section][lhs] = helpText;
                }
            }

            const size_t totalWidth = std::get<1>(getWindowSize());
            const size_t lhsWidth = maxLhsWidth;
            //                                                                              2 + 1 spaces
            const size_t rhsWidth = std::max(static_cast<size_t>(0), totalWidth - lhsWidth - 3);

            auto printSection = [lhsWidth,
                                 rhsWidth](const std::map<std::string, std::string> & entries) {
                for (auto & [lhs, rhs] : entries) {
                    auto wrapped = wrapText(rhs, rhsWidth);

                    for (auto const [index, wrappedComponent] : enumerate(wrapped)) {
                        if (index == 0) {
                            //                                                          2 + 1 spaces
                            auto compensatedLhsWidth = std::max(static_cast<size_t>(0), lhsWidth - 3);
                            std::cout
                                << std::format("  {:<{}} {}\n", lhs, compensatedLhsWidth, wrappedComponent);
                        } else {
                            //                                                          only 1 space
                            auto compensatedLhsWidth = std::max(static_cast<size_t>(0), lhsWidth - 1);
                            std::cout
                                << std::format("{:<{}} {}\n", " ", compensatedLhsWidth, wrappedComponent);
                        }
                    }
                }
            };

            printSection(specialEntries);
            printSection(generalEntries);
            for (auto & [section, entries] : perSectionEntries) {
                std::cout << "\n    " << section << " commands\n" << std::endl;
                printSection(entries);
            }

            return ProcessLineResult::PromptAgain;
        },
        {
            .aliases = {"?"},
            .help = "Print help about all commands (this content)",
        }
    );
}

void NixRepl::loadFile(const Path & path)
{
    ReplLoadable loadable{
        .spec = path,
        .kind = ReplLoadKind::File,
    };
    try {
        loaded.remove(loadable);
        loaded.push_back(loadable);
        Value v, v2;
        state.evalFile(
            state.aio.blockOn(lookupFileArg(evaluator, path)).unwrap(always_progresses), v
        );
        state.autoCallFunction(*autoArgs, v, v2, noPos);
        addAttrsToScope(v2);
    } catch (...) {
        // In case of failure, do not keep the loaded path.
        // Let the user reload it again later.
        loaded.remove(loadable);
        throw;
    }
}

void NixRepl::loadFlake(const std::string & flakeRefS)
{
    if (flakeRefS.empty())
        throw Error("cannot use ':load-flake' without a path specified. (Use '.' for the current working directory.)");

    auto flakeRef = parseFlakeRef(flakeRefS, absPath("."), true);
    if (evalSettings.pureEval && !flakeRef.input.isLocked())
        throw Error("cannot use ':load-flake' on locked flake reference '%s' (use --impure to override)", flakeRefS);

    ReplLoadable loadable{
        .spec = flakeRefS,
        .kind = ReplLoadKind::Flake,
    };

    Value v;

    try {
        loaded.remove(loadable);
        loaded.push_back(loadable);
        flake::callFlake(
            state,
            flake::lockFlake(
                state,
                flakeRef,
                flake::LockFlags{
                    .updateLockFile = false,
                    .useRegistries = !evalSettings.pureEval,
                    .allowUnlocked = !evalSettings.pureEval,
                }
            ),
            v
        );
        addAttrsToScope(v);
    } catch (...) {
        // In case of failure, do not keep the flake reference.
        // Let the user re-load it again later.
        loaded.remove(loadable);
        throw;
    }
}


void NixRepl::initEnv()
{
    env = &evaluator.mem.allocEnv(envSize);
    env->up = &evaluator.builtins.env;
    displ = 0;
    staticEnv->vars.clear();

    varNames.clear();
    for (auto & i : evaluator.builtins.staticEnv->vars)
        varNames.emplace(evaluator.symbols[i.first]);
}


void NixRepl::reloadFiles()
{
    if (loaded.empty() && getValues().empty()) {
        notice("No file to reload, skipping");
        return;
    }

    initEnv();

    loadFiles();
}


void NixRepl::loadFiles()
{
    std::list<ReplLoadable> saved{loaded};

    loaded.clear();

    for (auto const & [spec, kind] : saved) {
        switch (kind) {
            case ReplLoadKind::File:
                notice("Loading '%s'...", Magenta(spec));
                loadFile(spec);
                break;
            case ReplLoadKind::Flake:
                notice("Loading flake reference '%s'...", Magenta(spec));
                loadFlake(spec);
                break;
        }
    }

    for (auto & [i, what] : getValues()) {
        notice("Loading installable '%1%'...", Magenta(what));
        addAttrsToScope(i);
    }

    loadReplOverlays();
}

void NixRepl::loadReplOverlays()
{
    if (evalSettings.replOverlays.get().empty()) {
        return;
    }

    notice("Loading '%1%'...", "repl-overlays");
    auto replInitFilesFunction = getReplOverlaysEvalFunction();

    Value newAttrs;
    Value args[] = {replInitInfo(), bindingsToAttrs(), replOverlays()};
    state.callFunction(replInitFilesFunction, args, newAttrs, noPos);

    // n.b. this does in fact load the stuff into the environment twice (once
    // from the superset of the environment returned by repl-overlays and once
    // from the thing itself), but it's not fixable because clearEnv here could
    // lead to dangling references to the old environment in thunks.
    // https://git.lix.systems/lix-project/lix/issues/337#issuecomment-3745
    addAttrsToScope(newAttrs);
}

Value NixRepl::getReplOverlaysEvalFunction()
{
    if (replOverlaysEvalFunction && *replOverlaysEvalFunction) {
        return **replOverlaysEvalFunction;
    }

    auto evalReplInitFilesPath = CanonPath::root + "repl-overlays.nix";
    *replOverlaysEvalFunction = Value{};
    auto code =
        #include "repl-overlays.nix.gen.hh"
        ;
    auto & expr = evaluator.parseExprFromString(
        code,
        SourcePath(evalReplInitFilesPath),
        evaluator.builtins.staticEnv
    );

    state.eval(expr, **replOverlaysEvalFunction);

    return **replOverlaysEvalFunction;
}

Value NixRepl::replOverlays()
{
    Value replInits;
    auto replInitStorage = evaluator.mem.newList(evalSettings.replOverlays.get().size());
    replInits = {NewValueAs::list, replInitStorage};

    size_t i = 0;
    for (auto path : evalSettings.replOverlays.get()) {
        debug("Loading '%1%' path '%2%'...", "repl-overlays", path);
        SourcePath sourcePath((CanonPath(path)));

        // XXX(jade): This is a somewhat unsatisfying solution to
        // https://git.lix.systems/lix-project/lix/issues/777 which means that
        // the top level item in the repl-overlays file (that is, the lambda)
        // gets evaluated with pure eval off. This means that if you want to do
        // impure eval stuff, you will have to force it with builtins.seq.
        bool prevPureEval = evalSettings.pureEval.get();
        auto replInit = evalFile(sourcePath);
        evalSettings.pureEval.setDefault(prevPureEval);

        if (!replInit.isLambda()) {
            evaluator.errors
                .make<TypeError>(
                    "Expected `repl-overlays` entry %s to be a lambda but found %s: %s",
                    path,
                    showType(replInit),
                    ValuePrinter(state, replInit, errorPrintOptions)
                )
                .debugThrow();
        }

        if (auto attrs = dynamic_cast<AttrsPattern *>(replInit.lambda().fun->pattern.get());
            attrs && !attrs->ellipsis)
        {
            evaluator.errors
                .make<TypeError>(
                    "Expected first argument of %1% to have %2% to allow future versions of Lix to "
                    "add additional attributes to the argument",
                    "repl-overlays",
                    "..."
                )
                .atPos(replInit.lambda().fun->pos)
                .debugThrow();
        }

        replInitStorage->elems[i] = replInit;
        i++;
    }


    return replInits;
}

Value NixRepl::replInitInfo()
{
    auto builder = evaluator.buildBindings(2);

    Value currentSystem;
    currentSystem.mkString(evalSettings.getCurrentSystem());
    builder.insert(evaluator.symbols.create("currentSystem"), currentSystem);

    Value info;
    info.mkAttrs(builder.finish());
    return info;
}


template<typename T, typename NameFn, typename ValueFn>
void NixRepl::addToScope(T && things, NameFn nameFn, ValueFn valueFn)
{
    size_t added = 0;

    staticEnv->vars.unsafe_insert_bulk([&] (auto & map) {
        auto oldSize = map.size();
        for (auto && thing : things) {
            if (displ + 1 >= envSize)
                throw Error("environment full; cannot add more variables");

            const auto name = nameFn(thing);
            map.emplace_back(name, displ);
            env->values[displ++] = valueFn(thing);
            varNames.emplace(evaluator.symbols[name]);
            added++;
        }
        // safety: we sort the range that we inserted so that we don't have to push that
        // invariant up to the caller
        std::sort(map.begin() + oldSize, map.end());
    });

    if (added > 0) {
        notice("Added %1% variables.", added);
    }
}

void NixRepl::addAttrsToScope(Value & attrs)
{
    state.forceAttrs(attrs, noPos, "while evaluating an attribute set to be merged in the global scope");
    addToScope(
        *attrs.attrs(), [](const Attr & a) { return a.name; }, [](const Attr & a) { return a.value; }
    );
}

void NixRepl::addValMapToScope(const ValMap & attrs)
{
    addToScope(
        attrs,
        [&](auto & val) { return evaluator.symbols.create(val.first); },
        [&](auto & val) { return val.second; }
    );
}

void NixRepl::addVarToScope(const Symbol name, Value & v)
{
    if (displ >= envSize)
        throw Error("environment full; cannot add more variables");
    if (staticEnv->vars.insert_or_assign(name, displ).second) {
        notice("Updated %s.", evaluator.symbols[name]);
    } else {
        notice("Added %s.", evaluator.symbols[name]);
    }
    env->values[displ++] = v;
    varNames.emplace(evaluator.symbols[name]);
}

Value NixRepl::bindingsToAttrs()
{
    auto builder = evaluator.buildBindings(staticEnv->vars.size());
    for (auto & [symbol, displacement] : staticEnv->vars) {
        builder.insert(symbol, env->values[displacement]);
    }

    Value attrs;
    attrs.mkAttrs(builder.finish());
    return attrs;
}


Expr & NixRepl::parseString(std::string s)
{
    return evaluator.parseExprFromString(std::move(s), CanonPath::fromCwd(), staticEnv, featureSettings);
}

std::variant<std::unique_ptr<Expr>, ExprReplBindings> NixRepl::parseReplString(std::string s)
{
    return evaluator.parseReplInput(std::move(s), CanonPath::fromCwd(), staticEnv, featureSettings);
}


void NixRepl::evalString(std::string s, Value & v)
{
    Expr & e = parseString(s);
    e.eval(state, *env, v);
    state.forceValue(v, noPos);
}

Value NixRepl::evalFile(SourcePath & path)
{
    auto & expr = evaluator.parseExprFromFile(evaluator.paths.checkSourcePath(path), staticEnv);
    Value result;
    expr.eval(state, *env, result);
    state.forceValue(result, noPos);
    return result;
}

ReplExitStatus AbstractNixRepl::run(
    const SearchPath & searchPath,
    nix::ref<Store> store,
    EvalState & state,
    std::function<AnnotatedValues()> getValues,
    const ValMap & extraEnv,
    Bindings * autoArgs
)
{
    NixRepl repl(searchPath, store, state, getValues);

    repl.autoArgs = autoArgs;
    repl.initEnv();
    repl.addValMapToScope(extraEnv);
    return repl.mainLoop();
}

ReplExitStatus AbstractNixRepl::runSimple(EvalState & evalState, const ValMap & extraEnv)
{
    return run(
        {},
        evalState.aio.blockOn(openStore()),
        evalState,
        [] { return AnnotatedValues{}; },
        extraEnv,
        nullptr
    );
}

}
