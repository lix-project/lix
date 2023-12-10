#include <map>
#include <iostream>
#include <thread>
#include <filesystem>
#include <nix/eval-settings.hh>
#include <nix/config.h>
#include <nix/shared.hh>
#include <nix/store-api.hh>
#include <nix/eval.hh>
#include <nix/eval-inline.hh>
#include <nix/util.hh>
#include <nix/get-drvs.hh>
#include <nix/globals.hh>
#include <nix/common-eval-args.hh>
#include <nix/flake/flakeref.hh>
#include <nix/flake/flake.hh>
#include <nix/attr-path.hh>
#include <nix/derivations.hh>
#include <nix/local-fs-store.hh>
#include <nix/logging.hh>
#include <nix/error.hh>
#include <nix/installables.hh>
#include <nix/signals.hh>
#include <nix/terminal.hh>
#include <nix/path-with-outputs.hh>
#include <nix/installable-flake.hh>

#include <nix/value-to-json.hh>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include <nlohmann/json.hpp>

using namespace nix;
using namespace nlohmann;

// Safe to ignore - the args will be static.
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#elif __clang__
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif
struct MyArgs : virtual MixEvalArgs, virtual MixCommonArgs, virtual RootArgs {
    std::string releaseExpr;
    Path gcRootsDir;
    bool flake = false;
    bool fromArgs = false;
    bool meta = false;
    bool showTrace = false;
    bool impure = false;
    bool forceRecurse = false;
    bool checkCacheStatus = false;
    size_t nrWorkers = 1;
    size_t maxMemorySize = 4096;

    // usually in MixFlakeOptions
    flake::LockFlags lockFlags = {.updateLockFile = false,
                                  .writeLockFile = false,
                                  .useRegistries = false,
                                  .allowUnlocked = false};

    MyArgs() : MixCommonArgs("nix-eval-jobs") {
        addFlag({
            .longName = "help",
            .description = "show usage information",
            .handler = {[&]() {
                printf("USAGE: nix-eval-jobs [options] expr\n\n");
                for (const auto &[name, flag] : longFlags) {
                    if (hiddenCategories.count(flag->category)) {
                        continue;
                    }
                    printf("  --%-20s %s\n", name.c_str(),
                           flag->description.c_str());
                }
                ::exit(0);
            }},
        });

        addFlag({.longName = "impure",
                 .description = "allow impure expressions",
                 .handler = {&impure, true}});

        addFlag(
            {.longName = "force-recurse",
             .description = "force recursion (don't respect recurseIntoAttrs)",
             .handler = {&forceRecurse, true}});

        addFlag({.longName = "gc-roots-dir",
                 .description = "garbage collector roots directory",
                 .labels = {"path"},
                 .handler = {&gcRootsDir}});

        addFlag({.longName = "workers",
                 .description = "number of evaluate workers",
                 .labels = {"workers"},
                 .handler = {
                     [=, this](std::string s) { nrWorkers = std::stoi(s); }}});

        addFlag({.longName = "max-memory-size",
                 .description = "maximum evaluation memory size in megabyte "
                                "(4GiB per worker by default)",
                 .labels = {"size"},
                 .handler = {[=, this](std::string s) {
                     maxMemorySize = std::stoi(s);
                 }}});

        addFlag({.longName = "flake",
                 .description = "build a flake",
                 .handler = {&flake, true}});

        addFlag({.longName = "meta",
                 .description = "include derivation meta field in output",
                 .handler = {&meta, true}});

        addFlag(
            {.longName = "check-cache-status",
             .description =
                 "Check if the derivations are present locally or in "
                 "any configured substituters (i.e. binary cache). The "
                 "information "
                 "will be exposed in the `isCached` field of the JSON output.",
             .handler = {&checkCacheStatus, true}});

        addFlag({.longName = "show-trace",
                 .description =
                     "print out a stack trace in case of evaluation errors",
                 .handler = {&showTrace, true}});

        addFlag({.longName = "expr",
                 .shortName = 'E',
                 .description = "treat the argument as a Nix expression",
                 .handler = {&fromArgs, true}});

        // usually in MixFlakeOptions
        addFlag({
            .longName = "override-input",
            .description =
                "Override a specific flake input (e.g. `dwarffs/nixpkgs`).",
            .category = category,
            .labels = {"input-path", "flake-url"},
            .handler = {[&](std::string inputPath, std::string flakeRef) {
                // overriden inputs are unlocked
                lockFlags.allowUnlocked = true;
                lockFlags.inputOverrides.insert_or_assign(
                    flake::parseInputPath(inputPath),
                    parseFlakeRef(flakeRef, absPath("."), true));
            }},
        });

        expectArg("expr", &releaseExpr);
    }
};
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#elif __clang__
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif

static MyArgs myArgs;

static Value *releaseExprTopLevelValue(EvalState &state, Bindings &autoArgs) {
    Value vTop;

    if (myArgs.fromArgs) {
        Expr *e = state.parseExprFromString(
            myArgs.releaseExpr, state.rootPath(CanonPath::fromCwd()));
        state.eval(e, vTop);
    } else {
        state.evalFile(lookupFileArg(state, myArgs.releaseExpr), vTop);
    }

    auto vRoot = state.allocValue();

    state.autoCallFunction(autoArgs, vTop, *vRoot);

    return vRoot;
}

bool queryIsCached(Store &store, std::map<std::string, std::string> &outputs) {
    uint64_t downloadSize, narSize;
    StorePathSet willBuild, willSubstitute, unknown;

    std::vector<StorePathWithOutputs> paths;
    for (auto const &[key, val] : outputs) {
        paths.push_back(followLinksToStorePathWithOutputs(store, val));
    }

    store.queryMissing(toDerivedPaths(paths), willBuild, willSubstitute,
                       unknown, downloadSize, narSize);
    return willBuild.empty() && unknown.empty();
}

/* The fields of a derivation that are printed in json form */
struct Drv {
    std::string name;
    std::string system;
    std::string drvPath;
    bool isCached;
    std::map<std::string, std::string> outputs;
    std::map<std::string, std::set<std::string>> inputDrvs;
    std::optional<nlohmann::json> meta;

    Drv(std::string &attrPath, EvalState &state, DrvInfo &drvInfo) {

        auto localStore = state.store.dynamic_pointer_cast<LocalFSStore>();

        try {
            for (auto out : drvInfo.queryOutputs(true)) {
                if (out.second)
                    outputs[out.first] =
                        localStore->printStorePath(*out.second);
            }
        } catch (const std::exception &e) {
            throw EvalError("derivation '%s' does not have valid outputs: %s",
                            attrPath, e.what());
        }

        if (myArgs.meta) {
            nlohmann::json meta_;
            for (auto &metaName : drvInfo.queryMetaNames()) {
                NixStringContext context;
                std::stringstream ss;

                auto metaValue = drvInfo.queryMeta(metaName);
                // Skip non-serialisable types
                // TODO: Fix serialisation of derivations to store paths
                if (metaValue == 0) {
                    continue;
                }

                printValueAsJSON(state, true, *metaValue, noPos, ss, context);

                meta_[metaName] = nlohmann::json::parse(ss.str());
            }
            meta = meta_;
        }
        if (myArgs.checkCacheStatus) {
            isCached = queryIsCached(*localStore, outputs);
        }

        drvPath = localStore->printStorePath(drvInfo.requireDrvPath());

        auto drv = localStore->readDerivation(drvInfo.requireDrvPath());
        for (const auto &[inputDrvPath, inputNode] : drv.inputDrvs.map) {
            std::set<std::string> inputDrvOutputs;
            for (auto &outputName : inputNode.value) {
                inputDrvOutputs.insert(outputName);
            }
            inputDrvs[localStore->printStorePath(inputDrvPath)] =
                inputDrvOutputs;
        }
        name = drvInfo.queryName();
        system = drv.platform;
    }
};

static void to_json(nlohmann::json &json, const Drv &drv) {
    json = nlohmann::json{{"name", drv.name},
                          {"system", drv.system},
                          {"drvPath", drv.drvPath},
                          {"outputs", drv.outputs},
                          {"inputDrvs", drv.inputDrvs}};

    if (drv.meta.has_value()) {
        json["meta"] = drv.meta.value();
    }

    if (myArgs.checkCacheStatus) {
        json["isCached"] = drv.isCached;
    }
}

std::string attrPathJoin(json input) {
    return std::accumulate(input.begin(), input.end(), std::string(),
                           [](std::string ss, std::string s) {
                               // Escape token if containing dots
                               if (s.find(".") != std::string::npos) {
                                   s = "\"" + s + "\"";
                               }
                               return ss.empty() ? s : ss + "." + s;
                           });
}

[[nodiscard]] static int tryWriteLine(int fd, std::string s) {
    s += "\n";
    std::string_view sv{s};
    while (!sv.empty()) {
        checkInterrupt();
        ssize_t res = write(fd, sv.data(), sv.size());
        if (res == -1 && errno != EINTR) {
            return -errno;
        }
        if (res > 0) {
            sv.remove_prefix(res);
        }
    }
    return 0;
}

class LineReader {
  public:
    LineReader(int fd) {
        stream = fdopen(fd, "r");
        if (!stream) {
            throw Error("fdopen failed: %s", strerror(errno));
        }
    }

    ~LineReader() {
        fclose(stream);
        free(buffer);
    }

    LineReader(LineReader &&other) {
        stream = other.stream;
        other.stream = nullptr;
        buffer = other.buffer;
        other.buffer = nullptr;
        len = other.len;
        other.len = 0;
    }

    [[nodiscard]] std::string_view readLine() {
        ssize_t read = getline(&buffer, &len, stream);

        if (read == -1) {
            return {}; // Return an empty string_view in case of error
        }

        // Remove trailing newline
        return std::string_view(buffer, read - 1);
    }

  private:
    FILE *stream = nullptr;
    char *buffer = nullptr;
    size_t len = 0;
};

static void worker(ref<EvalState> state, Bindings &autoArgs, AutoCloseFD &to,
                   AutoCloseFD &from) {

    nix::Value *vRoot = [&]() {
        if (myArgs.flake) {
            auto [flakeRef, fragment, outputSpec] =
                parseFlakeRefWithFragmentAndExtendedOutputsSpec(
                    myArgs.releaseExpr, absPath("."));
            InstallableFlake flake{
                {}, state, std::move(flakeRef), fragment, outputSpec,
                {}, {},    myArgs.lockFlags};

            return flake.toValue(*state).first;
        } else {
            return releaseExprTopLevelValue(*state, autoArgs);
        }
    }();

    LineReader fromReader(from.release());

    while (true) {
        /* Wait for the collector to send us a job name. */
        if (tryWriteLine(to.get(), "next") < 0) {
            return; // main process died
        }

        auto s = fromReader.readLine();
        if (s == "exit") {
            break;
        }
        if (!hasPrefix(s, "do ")) {
            fprintf(stderr, "worker error: received invalid command '%s'\n",
                    s.data());
            abort();
        }
        auto path = json::parse(s.substr(3));
        auto attrPathS = attrPathJoin(path);

        debug("worker process %d at '%s'", getpid(), path);

        /* Evaluate it and send info back to the collector. */
        json reply = json{{"attr", attrPathS}, {"attrPath", path}};
        try {
            auto vTmp =
                findAlongAttrPath(*state, attrPathS, autoArgs, *vRoot).first;

            auto v = state->allocValue();
            state->autoCallFunction(autoArgs, *vTmp, *v);

            if (v->type() == nAttrs) {
                if (auto drvInfo = getDerivation(*state, *v, false)) {
                    auto drv = Drv(attrPathS, *state, *drvInfo);
                    reply.update(drv);

                    /* Register the derivation as a GC root.  !!! This
                       registers roots for jobs that we may have already
                       done. */
                    if (myArgs.gcRootsDir != "") {
                        Path root = myArgs.gcRootsDir + "/" +
                                    std::string(baseNameOf(drv.drvPath));
                        if (!pathExists(root)) {
                            auto localStore =
                                state->store
                                    .dynamic_pointer_cast<LocalFSStore>();
                            auto storePath =
                                localStore->parseStorePath(drv.drvPath);
                            localStore->addPermRoot(storePath, root);
                        }
                    }
                } else {
                    auto attrs = nlohmann::json::array();
                    bool recurse =
                        myArgs.forceRecurse ||
                        path.size() == 0; // Dont require `recurseForDerivations
                                          // = true;` for top-level attrset

                    for (auto &i :
                         v->attrs->lexicographicOrder(state->symbols)) {
                        const std::string &name = state->symbols[i->name];
                        attrs.push_back(name);

                        if (name == "recurseForDerivations" &&
                            !myArgs.forceRecurse) {
                            auto attrv =
                                v->attrs->get(state->sRecurseForDerivations);
                            recurse = state->forceBool(
                                *attrv->value, attrv->pos,
                                "while evaluating recurseForDerivations");
                        }
                    }
                    if (recurse)
                        reply["attrs"] = std::move(attrs);
                    else
                        reply["attrs"] = nlohmann::json::array();
                }
            } else {
                // We ignore everything that cannot be build
                reply["attrs"] = nlohmann::json::array();
            }
        } catch (EvalError &e) {
            auto err = e.info();
            std::ostringstream oss;
            showErrorInfo(oss, err, loggerSettings.showTrace.get());
            auto msg = oss.str();

            // Transmits the error we got from the previous evaluation
            // in the JSON output.
            reply["error"] = filterANSIEscapes(msg, true);
            // Don't forget to print it into the STDERR log, this is
            // what's shown in the Hydra UI.
            printError(e.msg());
        }

        if (tryWriteLine(to.get(), reply.dump()) < 0) {
            return; // main process died
        }

        /* If our RSS exceeds the maximum, exit. The collector will
           start a new process. */
        struct rusage r;
        getrusage(RUSAGE_SELF, &r);
        if ((size_t)r.ru_maxrss > myArgs.maxMemorySize * 1024)
            break;
    }

    if (tryWriteLine(to.get(), "restart") < 0) {
        return; // main process died
    };
}

typedef std::function<void(ref<EvalState> state, Bindings &autoArgs,
                           AutoCloseFD &to, AutoCloseFD &from)>
    Processor;

/* Auto-cleanup of fork's process and fds. */
struct Proc {
    AutoCloseFD to, from;
    Pid pid;

    Proc(const Processor &proc) {
        Pipe toPipe, fromPipe;
        toPipe.create();
        fromPipe.create();
        auto p = startProcess(
            [&,
             to{std::make_shared<AutoCloseFD>(std::move(fromPipe.writeSide))},
             from{
                 std::make_shared<AutoCloseFD>(std::move(toPipe.readSide))}]() {
                debug("created worker process %d", getpid());
                try {
                    auto state = std::make_shared<EvalState>(
                        myArgs.searchPath, openStore(*myArgs.evalStoreUrl));
                    Bindings &autoArgs = *myArgs.getAutoArgs(*state);
                    proc(ref<EvalState>(state), autoArgs, *to, *from);
                } catch (Error &e) {
                    nlohmann::json err;
                    auto msg = e.msg();
                    err["error"] = filterANSIEscapes(msg, true);
                    printError(msg);
                    if (tryWriteLine(to->get(), err.dump()) < 0) {
                        return; // main process died
                    };
                    // Don't forget to print it into the STDERR log, this is
                    // what's shown in the Hydra UI.
                    if (tryWriteLine(to->get(), "restart") < 0) {
                        return; // main process died
                    }
                }
            },
            ProcessOptions{.allowVfork = false});

        to = std::move(toPipe.writeSide);
        from = std::move(fromPipe.readSide);
        pid = p;
    }

    ~Proc() {}
};

struct State {
    std::set<json> todo = json::array({json::array()});
    std::set<json> active;
    std::exception_ptr exc;
};

void handleBrokenWorkerPipe(Proc &proc) {
    while (1) {
        int rc = waitpid(proc.pid, nullptr, WNOHANG);
        if (rc == 0) {
            proc.pid = -1; // we already took the process status from Proc, no
                           // need to wait for it again to avoid error messages
            throw Error("BUG: worker pipe closed but worker still running?");
        } else if (rc == -1) {
            proc.pid = -1;
            throw Error("BUG: waitpid waiting for worker failed: %s",
                        strerror(errno));
        } else {
            if (WIFEXITED(rc)) {
                proc.pid = -1;
                throw Error("evaluation worker exited with %d",
                            WEXITSTATUS(rc));
            } else if (WIFSIGNALED(rc)) {
                proc.pid = -1;
                if (WTERMSIG(rc) == SIGKILL) {
                    throw Error("evaluation worker killed by SIGKILL, maybe "
                                "memory limit reached?");
                }
                throw Error("evaluation worker killed by signal %d",
                            WTERMSIG(rc));
            } // else ignore WIFSTOPPED and WIFCONTINUED
        }
    }
}

std::function<void()> collector(Sync<State> &state_,
                                std::condition_variable &wakeup) {
    return [&]() {
        try {
            std::optional<std::unique_ptr<Proc>> proc_;
            std::optional<std::unique_ptr<LineReader>> fromReader_;

            while (true) {
                if (!proc_.has_value()) {
                    proc_ = std::make_unique<Proc>(worker);
                    fromReader_ = std::make_unique<LineReader>(
                        proc_.value()->from.release());
                }
                auto proc = std::move(proc_.value());
                auto fromReader = std::move(fromReader_.value());

                /* Check whether the existing worker process is still there. */
                auto s = fromReader->readLine();
                if (s == "") {
                    handleBrokenWorkerPipe(*proc.get());
                } else if (s == "restart") {
                    proc_ = std::nullopt;
                    fromReader_ = std::nullopt;
                    continue;
                } else if (s != "next") {
                    try {
                        auto json = json::parse(s);
                        throw Error("worker error: %s",
                                    (std::string)json["error"]);
                    } catch (const json::exception &e) {
                        throw Error(
                            "Received invalid JSON from worker: %s '%s'",
                            e.what(), s);
                    }
                }

                /* Wait for a job name to become available. */
                json attrPath;

                while (true) {
                    checkInterrupt();
                    auto state(state_.lock());
                    if ((state->todo.empty() && state->active.empty()) ||
                        state->exc) {
                        if (tryWriteLine(proc->to.get(), "exit") < 0) {
                            handleBrokenWorkerPipe(*proc.get());
                        }
                        return;
                    }
                    if (!state->todo.empty()) {
                        attrPath = *state->todo.begin();
                        state->todo.erase(state->todo.begin());
                        state->active.insert(attrPath);
                        break;
                    } else
                        state.wait(wakeup);
                }

                /* Tell the worker to evaluate it. */
                if (tryWriteLine(proc->to.get(), "do " + attrPath.dump()) < 0) {
                    handleBrokenWorkerPipe(*proc.get());
                }

                /* Wait for the response. */
                auto respString = fromReader->readLine();
                if (respString == "") {
                    handleBrokenWorkerPipe(*proc.get());
                }
                json response;
                try {
                    response = json::parse(respString);
                    if (response.find("error") != response.end()) {
                        throw Error("worker error: %s",
                                    (std::string)response["error"]);
                    }
                } catch (const json::exception &e) {
                    throw Error("Received invalid JSON from worker: %s '%s'",
                                e.what(), respString);
                }

                /* Handle the response. */
                std::vector<json> newAttrs;
                if (response.find("attrs") != response.end()) {
                    for (auto &i : response["attrs"]) {
                        json newAttr = json(response["attrPath"]);
                        newAttr.emplace_back(i);
                        newAttrs.push_back(newAttr);
                    }
                } else {
                    auto state(state_.lock());
                    std::cout << respString << "\n" << std::flush;
                }

                proc_ = std::move(proc);
                fromReader_ = std::move(fromReader);

                /* Add newly discovered job names to the queue. */
                {
                    auto state(state_.lock());
                    state->active.erase(attrPath);
                    for (auto p : newAttrs) {
                        state->todo.insert(p);
                    }
                    wakeup.notify_all();
                }
            }
        } catch (...) {
            auto state(state_.lock());
            state->exc = std::current_exception();
            wakeup.notify_all();
        }
    };
}

int main(int argc, char **argv) {
    /* Prevent undeclared dependencies in the evaluation via
       $NIX_PATH. */
    unsetenv("NIX_PATH");

    /* We are doing the garbage collection by killing forks */
    setenv("GC_DONT_GC", "1", 1);

    return handleExceptions(argv[0], [&]() {
        initNix();
        initGC();

        myArgs.parseCmdline(argvToStrings(argc, argv), 0);

        /* FIXME: The build hook in conjunction with import-from-derivation is
         * causing "unexpected EOF" during eval */
        settings.builders = "";

        /* Prevent access to paths outside of the Nix search path and
           to the environment. */
        evalSettings.restrictEval = false;

        /* When building a flake, use pure evaluation (no access to
           'getEnv', 'currentSystem' etc. */
        if (myArgs.impure) {
            evalSettings.pureEval = false;
        } else if (myArgs.flake) {
            evalSettings.pureEval = true;
        }

        if (myArgs.releaseExpr == "")
            throw UsageError("no expression specified");

        if (myArgs.gcRootsDir == "") {
            printMsg(lvlError, "warning: `--gc-roots-dir' not specified");
        } else {
            myArgs.gcRootsDir = std::filesystem::absolute(myArgs.gcRootsDir);
        }

        if (myArgs.showTrace) {
            loggerSettings.showTrace.assign(true);
        }

        Sync<State> state_;

        /* Start a collector thread per worker process. */
        std::vector<std::thread> threads;
        std::condition_variable wakeup;
        for (size_t i = 0; i < myArgs.nrWorkers; i++)
            threads.emplace_back(std::thread(collector(state_, wakeup)));

        for (auto &thread : threads)
            thread.join();

        auto state(state_.lock());

        if (state->exc)
            std::rethrow_exception(state->exc);
    });
}
