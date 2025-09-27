#include <cstring>
#include <iostream>
#include <regex>
#include <sstream>
#include <vector>
#include <map>

#include "lix/libstore/parsed-derivations.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/current-process.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/get-drvs.hh"
#include "lix/libcmd/common-eval-args.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libcmd/legacy.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/shlex.hh"
#include "nix-build.hh"
#include "lix/libstore/temporary-dir.hh"

extern char * * environ __attribute__((weak)); // Man what even is this

namespace nix {

using namespace std::string_literals;

static void main_nix_build(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    auto dryRun = false;
    auto runEnv = std::regex_search(programName, regex::parse("nix-shell$"));
    auto pure = false;
    auto fromArgs = false;
    auto packages = false;
    // Same condition as bash uses for interactive shells
    auto interactive = isatty(STDIN_FILENO) && isatty(STDERR_FILENO);
    Strings attrPaths;
    Strings left;
    BuildMode buildMode = bmNormal;
    bool readStdin = false;

    std::string envCommand; // interactive shell
    Strings envExclude;

    auto myName = runEnv ? "nix-shell" : "nix-build";

    auto inShebang = false;
    std::string script;
    std::vector<std::string> savedArgs;

    std::string outLink = "./result";

    // List of environment variables kept for --pure
    std::set<std::string> keepVars{
        "HOME", "XDG_RUNTIME_DIR", "USER", "LOGNAME", "DISPLAY",
        "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "PATH", "TERM", "IN_NIX_SHELL",
        "NIX_SHELL_PRESERVE_PROMPT", "TZ", "PAGER", "NIX_BUILD_SHELL", "SHLVL",
        "http_proxy", "https_proxy", "ftp_proxy", "all_proxy", "no_proxy"
    };

    // Heuristic to see if we're invoked as a shebang script, namely,
    // if we have at least one argument, it's the name of an
    // executable file, and it starts with "#!".
    if (runEnv && !argv.empty()) {
        script = argv.front();
        try {
            auto lines = tokenizeString<Strings>(readFile(script), "\n");
            if (std::regex_search(lines.front(), regex::parse("^#!"))) {
                lines.pop_front();
                inShebang = true;
                savedArgs = {std::next(argv.begin()), argv.end()};
                argv.clear();
                for (auto line : lines) {
                    line = chomp(line);
                    std::smatch match;
                    if (std::regex_match(line, match, regex::parse("^#!\\s*nix-shell\\s+(.*)$")))
                        for (const auto & word : shell_split(match[1].str()))
                            argv.push_back(word);
                }
            }
        } catch (SysError &) { }
    }

    struct MyArgs : LegacyArgs, MixEvalArgs
    {
        using LegacyArgs::LegacyArgs;
    };

    MyArgs myArgs(aio, myName, [&](Strings::iterator & arg, const Strings::iterator & end) {
        if (*arg == "--help") {
            showManPage(myName);
        }

        else if (*arg == "--version")
            printVersion(myName);

        else if (*arg == "--add-drv-link" || *arg == "--indirect")
            ; // obsolete

        else if (*arg == "--no-out-link" || *arg == "--no-link")
            outLink = "";

        else if (*arg == "--attr" || *arg == "-A")
            attrPaths.push_back(getArg(*arg, arg, end));

        else if (*arg == "--drv-link")
            getArg(*arg, arg, end); // obsolete

        else if (*arg == "--out-link" || *arg == "-o")
            outLink = getArg(*arg, arg, end);

        else if (*arg == "--dry-run")
            dryRun = true;

        else if (*arg == "--run-env") // obsolete
            runEnv = true;

        else if (runEnv && (*arg == "--command" || *arg == "--run")) {
            if (*arg == "--run")
                interactive = false;
            envCommand = getArg(*arg, arg, end) + "\nexit";
        }

        else if (*arg == "--check")
            buildMode = bmCheck;

        else if (*arg == "--exclude")
            envExclude.push_back(getArg(*arg, arg, end));

        else if (*arg == "--expr" || *arg == "-E")
            fromArgs = true;

        else if (*arg == "--pure") pure = true;
        else if (*arg == "--impure") pure = false;

        else if (runEnv && (*arg == "--packages" || *arg == "-p"))
            packages = true;

        else if (inShebang && *arg == "-i") {
            auto interpreter = getArg(*arg, arg, end);
            interactive = false;
            auto execArgs = "";

            // Überhack to support Perl. Perl examines the shebang and
            // executes it unless it contains the string "perl" or "indir",
            // or (undocumented) argv[0] does not contain "perl". Exploit
            // the latter by doing "exec -a".
            if (std::regex_search(interpreter, regex::parse("perl")))
                execArgs = "-a PERL";

            std::ostringstream joined;
            for (const auto & i : savedArgs)
                joined << shellEscape(i) << ' ';

            if (std::regex_search(interpreter, regex::parse("ruby"))) {
                // Hack for Ruby. Ruby also examines the shebang. It tries to
                // read the shebang to understand which packages to read from. Since
                // this is handled via nix-shell -p, we wrap our ruby script execution
                // in ruby -e 'load' which ignores the shebangs.
                envCommand = fmt("exec %1% %2% -e 'load(ARGV.shift)' -- %3% %4%", execArgs, interpreter, shellEscape(script), joined.str());
            } else {
                envCommand = fmt("exec %1% %2% %3% %4%", execArgs, interpreter, shellEscape(script), joined.str());
            }
        }

        else if (*arg == "--keep")
            keepVars.insert(getArg(*arg, arg, end));

        else if (*arg == "-")
            readStdin = true;

        else if (*arg != "" && arg->at(0) == '-')
            return false;

        else
            left.push_back(*arg);

        return true;
    });

    myArgs.parseCmdline(argv);

    if (packages && fromArgs)
        throw UsageError("'-p' and '-E' are mutually exclusive");

    AutoDelete tmpDir(createTempDir("", myName));
    if (outLink.empty())
        outLink = (Path) tmpDir + "/result";

    auto store = aio.blockOn(openStore());
    auto evalStore = myArgs.evalStoreUrl ? aio.blockOn(openStore(*myArgs.evalStoreUrl)) : store;

    auto evaluator = std::make_unique<Evaluator>(aio, myArgs.searchPath, evalStore, store);
    evaluator->repair = myArgs.repair;
    auto state = evaluator->begin(aio);
    if (myArgs.repair) buildMode = bmRepair;

    auto autoArgs = myArgs.getAutoArgs(*evaluator);

    auto autoArgsWithInNixShell = autoArgs;
    if (runEnv) {
        auto newArgs = evaluator->buildBindings(autoArgsWithInNixShell->size() + 1);
        newArgs.alloc("inNixShell").mkBool(true);
        for (auto & i : *autoArgs) newArgs.insert(i);
        autoArgsWithInNixShell = newArgs.finish();
    }

    if (packages) {
        std::ostringstream joined;
        joined << "{...}@args: with import <nixpkgs> args; (pkgs.runCommandCC or pkgs.runCommand) \"shell\" { buildInputs = [ ";
        for (const auto & i : left)
            joined << '(' << i << ") ";
        joined << "]; } \"\"";
        fromArgs = true;
        left = {joined.str()};
    } else if (!fromArgs) {
        if (left.empty() && runEnv && pathExists("shell.nix"))
            left = {"shell.nix"};
        if (left.empty())
            left = {"default.nix"};
    }

    if (runEnv)
        setenv("IN_NIX_SHELL", pure ? "pure" : "impure", 1);

    DrvInfos drvs;

    /* Parse the expressions. */
    std::vector<std::reference_wrapper<Expr>> exprs;

    if (readStdin)
        exprs = {evaluator->parseStdin()};
    else
        for (auto i : left) {
            if (fromArgs)
                exprs.push_back(evaluator->parseExprFromString(std::move(i), CanonPath::fromCwd()));
            else {
                auto absolute = i;
                try {
                    absolute = canonPath(absPath(i), true);
                } catch (Error & e) {};
                auto [path, outputNames] = parsePathWithOutputs(absolute);
                if (evalStore->isStorePath(path) && path.ends_with(".drv"))
                    drvs.push_back(aio.blockOn(DrvInfo::create(evalStore, absolute)));
                else
                    /* If we're in a #! script, interpret filenames
                       relative to the script. */
                    exprs.push_back(evaluator->parseExprFromFile(
                        evaluator->paths.resolveExprPath(aio.blockOn(lookupFileArg(
                            *evaluator,
                            inShebang && !packages ? absPath(i, absPath(dirOf(script))) : i
                        )).unwrap())
                    ));
            }
        }

    /* Evaluate them into derivations. */
    if (attrPaths.empty()) attrPaths = {""};

    for (auto e : exprs) {
        Value vRoot;
        state->eval(e, vRoot);

        std::function<bool(const Value & v)> takesNixShellAttr;
        takesNixShellAttr = [&](const Value & v) {
            if (!runEnv) {
                return false;
            }
            bool add = false;
            if (v.type() == nFunction) {
                if (auto pattern = dynamic_cast<AttrsPattern *>(v.lambda().fun->pattern.get())) {
                    for (auto & i : pattern->formals) {
                        if (evaluator->symbols[i.name] == "inNixShell") {
                            add = true;
                            break;
                        }
                    }
                }
            }
            return add;
        };

        for (auto & i : attrPaths) {
            Value v(
                findAlongAttrPath(
                    *state, i, takesNixShellAttr(vRoot) ? *autoArgsWithInNixShell : *autoArgs, vRoot
                )
                    .first
            );
            state->forceValue(v, noPos);
            getDerivations(
                *state,
                v,
                "",
                takesNixShellAttr(v) ? *autoArgsWithInNixShell : *autoArgs,
                drvs,
                false
            );
        }
    }

    evaluator->maybePrintStats();

    auto buildPaths = [&](const std::vector<DerivedPath> & paths) {
        /* Note: we do this even when !printMissing to efficiently
           fetch binary cache data. */
        uint64_t downloadSize, narSize;
        StorePathSet willBuild, willSubstitute, unknown;
        aio.blockOn(store->queryMissing(paths,
            willBuild, willSubstitute, unknown, downloadSize, narSize));

        if (settings.printMissing) {
            aio.blockOn(printMissing(
                ref<Store>(store), willBuild, willSubstitute, unknown, downloadSize, narSize
            ));
        }

        if (!dryRun)
            aio.blockOn(store->buildPaths(paths, buildMode, evalStore));
    };

    if (runEnv) {
        if (drvs.size() != 1)
            throw UsageError("nix-shell requires a single derivation");

        auto & drvInfo = drvs.front();
        auto drv = aio.blockOn(evalStore->derivationFromPath(drvInfo.requireDrvPath(*state)));

        std::vector<DerivedPath> pathsToBuild;
        RealisedPath::Set pathsToCopy;

        /* Figure out what bash shell to use. If $NIX_BUILD_SHELL
           is not set, then build bashInteractive from
           <nixpkgs>. */
        auto shell = getEnv("NIX_BUILD_SHELL");
        std::optional<StorePath> shellDrv;

        if (!shell) {

            try {
                auto & expr = evaluator->parseExprFromString(
                    "(import <nixpkgs> {}).bashInteractive",
                    CanonPath::fromCwd());

                Value v;
                state->eval(expr, v);

                auto drv = getDerivation(*state, v, false);
                if (!drv)
                    throw Error("the 'bashInteractive' attribute in <nixpkgs> did not evaluate to a derivation");

                auto bashDrv = drv->requireDrvPath(*state);
                pathsToBuild.push_back(DerivedPath::Built {
                    .drvPath = makeConstantStorePath(bashDrv),
                    .outputs = OutputsSpec::Names {"out"},
                });
                pathsToCopy.insert(bashDrv);
                shellDrv = bashDrv;

            } catch (Error & e) {
                logError(e.info());
                notice("will use bash from your environment");
                shell = "bash";
            }
        }

        auto accumDerivedPath = [&](SingleDerivedPath::Opaque inputDrv, const StringSet & inputNode) {
            if (!inputNode.empty())
                pathsToBuild.push_back(DerivedPath::Built {
                    .drvPath = inputDrv,
                    .outputs = OutputsSpec::Names { inputNode },
                });
        };

        // Build or fetch all dependencies of the derivation.
        for (const auto & [inputDrv0, inputNode] : drv.inputDrvs) {
            // To get around lambda capturing restrictions in the
            // standard.
            const auto & inputDrv = inputDrv0;
            if (std::all_of(envExclude.cbegin(), envExclude.cend(),
                    [&](const std::string & exclude) {
                        return !std::regex_search(store->printStorePath(inputDrv), regex::parse(exclude));
                    }))
            {
                accumDerivedPath(makeConstantStorePath(inputDrv), inputNode);
                pathsToCopy.insert(inputDrv);
            }
        }
        for (const auto & src : drv.inputSrcs) {
            pathsToBuild.emplace_back(DerivedPath::Opaque{src});
            pathsToCopy.insert(src);
        }

        buildPaths(pathsToBuild);

        if (dryRun) return;

        if (shellDrv) {
            auto shellDrvOutputs =
                aio.blockOn(store->queryDerivationOutputMap(shellDrv.value(), &*evalStore));
            shell = store->printStorePath(shellDrvOutputs.at("out")) + "/bin/bash";
        }

        // Set the environment.
        auto env = getEnv();

        if (pure) {
            decltype(env) newEnv;
            for (auto & i : env)
                if (keepVars.count(i.first))
                    newEnv.emplace(i);
            env = newEnv;
            // NixOS hack: prevent /etc/bashrc from sourcing /etc/profile.
            env["__ETC_PROFILE_SOURCED"] = "1";
        }

        // Don't use defaultTempDir() here! We want to preserve the user's TMPDIR for the shell
        env["NIX_BUILD_TOP"] = env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = getEnvNonEmpty("TMPDIR").value_or("/tmp");
        env["NIX_STORE"] = store->config().storeDir;
        env["NIX_BUILD_CORES"] = std::to_string(settings.buildCores);

        auto passAsFile = tokenizeString<StringSet>(getOr(drv.env, "passAsFile", ""));

        bool keepTmp = false;
        int fileNr = 0;

        for (auto & var : drv.env)
            if (passAsFile.count(var.first)) {
                keepTmp = true;
                auto fn = ".attr-" + std::to_string(fileNr++);
                Path p = (Path) tmpDir + "/" + fn;
                writeFile(p, var.second);
                env[var.first + "Path"] = p;
            } else
                env[var.first] = var.second;

        std::string structuredAttrsRC;

        if (env.count("__json")) {
            StorePathSet inputs;

            auto accumInputClosure = [&](const StorePath & inputDrv, const StringSet & inputNode) {
                auto outputs =
                    aio.blockOn(store->queryDerivationOutputMap(inputDrv, &*evalStore));
                for (auto & i : inputNode) {
                    auto o = outputs.at(i);
                    aio.blockOn(store->computeFSClosure(o, inputs));
                }
            };

            for (const auto & [inputDrv, inputNode] : drv.inputDrvs)
                accumInputClosure(inputDrv, inputNode);

            ParsedDerivation parsedDrv(drvInfo.requireDrvPath(*state), drv);

            if (auto structAttrs = aio.blockOn(parsedDrv.prepareStructuredAttrs(*store, inputs))) {
                auto json = structAttrs.value();
                structuredAttrsRC = writeStructuredAttrsShell(json);

                auto attrsJSON = (Path) tmpDir + "/.attrs.json";
                writeFile(attrsJSON, json.dump());

                auto attrsSH = (Path) tmpDir + "/.attrs.sh";
                writeFile(attrsSH, structuredAttrsRC);

                env["NIX_ATTRS_SH_FILE"] = attrsSH;
                env["NIX_ATTRS_JSON_FILE"] = attrsJSON;
                keepTmp = true;
            }
        }

        /* Run a shell using the derivation's environment.  For
           convenience, source $stdenv/setup to setup additional
           environment variables and shell functions.  Also don't
           lose the current $PATH directories. */
        auto rcfile = (Path) tmpDir + "/rc";
        auto tz = getEnv("TZ");
        std::string rc = fmt(
            R"(_nix_shell_clean_tmpdir() { command rm -rf %1%; }; )"
            "%2%"
            "%3%"
            // always clear PATH.
            // when nix-shell is run impure, we rehydrate it with the `p=$PATH` above
            "unset PATH;"
            "dontAddDisableDepTrack=1;\n",
            shellEscape(tmpDir),
            (keepTmp
                ? "trap _nix_shell_clean_tmpdir EXIT; "
                  "exitHooks+=(_nix_shell_clean_tmpdir); "
                  "failureHooks+=(_nix_shell_clean_tmpdir); "
                : "_nix_shell_clean_tmpdir; "),
            (pure
                ? ""
                : "[ -n \"$PS1\" ] && [ -e ~/.bashrc ] && source ~/.bashrc; p=$PATH; ")
        );
        rc += structuredAttrsRC;
        rc += fmt(
            "\n[ -e $stdenv/setup ] && source $stdenv/setup; "
            "%1%"
            "PATH=%2%:\"$PATH\"; "
            "SHELL=%3%; "
            "BASH=%3%; "
            "set +e; "
            R"s([ -n "$PS1" -a -z "$NIX_SHELL_PRESERVE_PROMPT" ] && )s"
            "%4%"
            "if [ \"$(type -t runHook)\" = function ]; then runHook shellHook; fi; "
            "unset NIX_ENFORCE_PURITY; "
            "shopt -u nullglob; "
            "unset TZ; %5%"
            "shopt -s execfail;"
            "%6%",
            (pure ? "" : "PATH=$PATH:$p; unset p; "),
            shellEscape(dirOf(*shell)),
            shellEscape(*shell),
            (getuid() == 0 ? R"s(PS1='\n\[\033[1;31m\][nix-shell:\w]\$\[\033[0m\] '; )s"
                           : R"s(PS1='\n\[\033[1;32m\][nix-shell:\w]\$\[\033[0m\] '; )s"),
            (tz.has_value()
                ? (std::string("export TZ=") + shellEscape(*tz) + "; ")
                : ""),
            envCommand
        );
        vomit("Sourcing nix-shell with file %s and contents:\n%s", rcfile, rc);
        writeFile(rcfile, rc);

        Strings envStrs;
        for (auto & i : env)
            envStrs.push_back(i.first + "=" + i.second);

        auto args = interactive
            ? Strings{"bash", "--rcfile", rcfile}
            : Strings{"bash", rcfile};

        auto envPtrs = stringsToCharPtrs(envStrs);

        environ = envPtrs.data();

        auto argPtrs = stringsToCharPtrs(args);

        restoreProcessContext();

        logger->pause();

        printMsg(lvlChatty, "running shell: %s", concatMapStringsSep(" ", args, shellEscape));

        execvp(shell->c_str(), argPtrs.data());

        throw SysError("executing shell '%s'", *shell);
    }

    else {

        std::vector<DerivedPath> pathsToBuild;
        std::vector<std::pair<StorePath, std::string>> pathsToBuildOrdered;
        RealisedPath::Set drvsToCopy;

        std::map<StorePath, std::pair<size_t, StringSet>> drvMap;

        for (auto & drvInfo : drvs) {
            auto drvPath = drvInfo.requireDrvPath(*state);

            auto outputName = drvInfo.queryOutputName(*state);
            if (outputName == "")
                throw Error("derivation '%s' lacks an 'outputName' attribute", store->printStorePath(drvPath));

            pathsToBuild.push_back(DerivedPath::Built{
                .drvPath = makeConstantStorePath(drvPath),
                .outputs = OutputsSpec::Names{outputName},
            });
            pathsToBuildOrdered.push_back({drvPath, {outputName}});
            drvsToCopy.insert(drvPath);

            auto i = drvMap.find(drvPath);
            if (i != drvMap.end())
                i->second.second.insert(outputName);
            else
                drvMap[drvPath] = {drvMap.size(), {outputName}};
        }

        buildPaths(pathsToBuild);

        if (dryRun) return;

        std::vector<StorePath> outPaths;

        for (auto & [drvPath, outputName] : pathsToBuildOrdered) {
            auto & [counter, _wantedOutputs] = drvMap.at({drvPath});
            std::string drvPrefix = outLink;
            if (counter)
                drvPrefix += fmt("-%d", counter + 1);

            auto builtOutputs =
                aio.blockOn(store->queryDerivationOutputMap(drvPath, &*evalStore));

            auto outputPath = builtOutputs.at(outputName);

            if (auto store2 = store.try_cast_shared<LocalFSStore>()) {
                std::string symlink = drvPrefix;
                if (outputName != "out") symlink += "-" + outputName;
                aio.blockOn(store2->addPermRoot(outputPath, absPath(symlink)));
            }

            outPaths.push_back(outputPath);
        }

        logger->pause();

        for (auto & path : outPaths)
            std::cout << store->printStorePath(path) << '\n';
    }
}

void registerLegacyNixBuildAndNixShell() {
    LegacyCommandRegistry::add("nix-build", main_nix_build);
    LegacyCommandRegistry::add("nix-shell", main_nix_build);
}

}
