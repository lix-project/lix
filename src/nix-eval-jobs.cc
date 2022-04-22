#include <map>
#include <iostream>
#include <thread>

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

#include <nix/value-to-json.hh>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include <nlohmann/json.hpp>

using namespace nix;

typedef enum { evalAuto, evalImpure, evalPure } pureEval;

// Safe to ignore - the args will be static.
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
struct MyArgs : MixEvalArgs, MixCommonArgs
{
    Path releaseExpr;
    Path gcRootsDir;
    bool flake = false;
    bool meta = false;
    bool showTrace = false;
    size_t nrWorkers = 1;
    size_t maxMemorySize = 4096;
    pureEval evalMode = evalAuto;

    MyArgs() : MixCommonArgs("nix-eval-jobs")
    {
        addFlag({
            .longName = "help",
            .description = "show usage information",
            .handler = {[&]() {
                printf("USAGE: nix-eval-jobs [options] expr\n\n");
                for (const auto & [name, flag] : longFlags) {
                    if (hiddenCategories.count(flag->category)) {
                        continue;
                    }
                    printf("  --%-20s %s\n", name.c_str(), flag->description.c_str());
                }
                ::exit(0);
            }},
        });

        addFlag({
            .longName = "impure",
            .description = "set evaluation mode",
            .handler = {[&]() {
                evalMode = evalImpure;
            }},
        });

        addFlag({
            .longName = "gc-roots-dir",
            .description = "garbage collector roots directory",
            .labels = {"path"},
            .handler = {&gcRootsDir}
        });

        addFlag({
            .longName = "workers",
            .description = "number of evaluate workers",
            .labels = {"workers"},
            .handler = {[=](std::string s) {
                nrWorkers = std::stoi(s);
            }}
        });

        addFlag({
            .longName = "max-memory-size",
            .description = "maximum evaluation memory size",
            .labels = {"size"},
            .handler = {[=](std::string s) {
                maxMemorySize = std::stoi(s);
            }}
        });

        addFlag({
            .longName = "flake",
            .description = "build a flake",
            .handler = {&flake, true}
        });

        addFlag({
            .longName = "meta",
            .description = "include derivation meta field in output",
            .handler = {&meta, true}
        });

        addFlag({
            .longName = "show-trace",
            .description = "print out a stack trace in case of evaluation errors",
            .handler = {&showTrace, true}
        });

        expectArg("expr", &releaseExpr);
    }
};
#pragma GCC diagnostic warning "-Wnon-virtual-dtor"
#pragma clang diagnostic warning "-Wnon-virtual-dtor"

static MyArgs myArgs;

static Value* releaseExprTopLevelValue(EvalState & state, Bindings & autoArgs) {
    Value vTop;

    state.evalFile(lookupFileArg(state, myArgs.releaseExpr), vTop);

    auto vRoot = state.allocValue();

    state.autoCallFunction(autoArgs, vTop, *vRoot);

    return vRoot;
}

static Value* flakeTopLevelValue(EvalState & state, Bindings & autoArgs) {
    using namespace flake;

    auto [flakeRef, fragment] = parseFlakeRefWithFragment(myArgs.releaseExpr, absPath("."));

    auto vFlake = state.allocValue();

    auto lockedFlake = lockFlake(state, flakeRef,
        LockFlags {
            .updateLockFile = false,
            .useRegistries = false,
            .allowMutable = false,
        });

    callFlake(state, lockedFlake, *vFlake);

    auto vOutputs = vFlake->attrs->get(state.symbols.create("outputs"))->value;
    state.forceValue(*vOutputs, noPos);
    auto vTop = *vOutputs;

    if (fragment.length() > 0) {
        Bindings & bindings(*state.allocBindings(0));
        auto [nTop, pos] = findAlongAttrPath(state, fragment, bindings, vTop);
        if (!nTop)
            throw Error("error: attribute '%s' missing", nTop);
        vTop = *nTop;
    }

    auto vRoot = state.allocValue();
    state.autoCallFunction(autoArgs, vTop, *vRoot);

    return vRoot;
}

Value * topLevelValue(EvalState & state, Bindings & autoArgs) {
    return myArgs.flake
        ? flakeTopLevelValue(state, autoArgs)
        : releaseExprTopLevelValue(state, autoArgs);
}

static void worker(
    EvalState & state,
    Bindings & autoArgs,
    AutoCloseFD & to,
    AutoCloseFD & from)
{
    auto vRoot = topLevelValue(state, autoArgs);

    while (true) {
        /* Wait for the collector to send us a job name. */
        writeLine(to.get(), "next");

        auto s = readLine(from.get());
        if (s == "exit") break;
        if (!hasPrefix(s, "do ")) abort();
        std::string attrPath(s, 3);

        debug("worker process %d at '%s'", getpid(), attrPath);

        /* Evaluate it and send info back to the collector. */
        nlohmann::json reply;
        reply["attr"] = attrPath;

        try {
            auto vTmp = findAlongAttrPath(state, attrPath, autoArgs, *vRoot).first;

            auto v = state.allocValue();
            state.autoCallFunction(autoArgs, *vTmp, *v);

            if (auto drv = getDerivation(state, *v, false)) {

                if (drv->querySystem() == "unknown")
                    throw EvalError("derivation must have a 'system' attribute");

                auto localStore = state.store.dynamic_pointer_cast<LocalFSStore>();
                auto drvPath = localStore->printStorePath(drv->requireDrvPath());
                auto storePath = localStore->parseStorePath(drvPath);
                auto outputs = drv->queryOutputs();

                reply["name"] = drv->queryName();
                reply["system"] = drv->querySystem();
                reply["drvPath"] = drvPath;
                for (auto out : outputs){
                    if (out.second) {
                        reply["outputs"][out.first] = localStore->printStorePath(*out.second);
                    }
                }

                if (myArgs.meta) {
                    nlohmann::json meta;
                    for (auto & name : drv->queryMetaNames()) {
                      PathSet context;
                      std::stringstream ss;

                      auto metaValue = drv->queryMeta(name);
                      // Skip non-serialisable types
                      // TODO: Fix serialisation of derivations to store paths
                      if (metaValue == 0) {
                        continue;
                      }

                      printValueAsJSON(state, true, *metaValue, noPos, ss, context);
                      nlohmann::json field = nlohmann::json::parse(ss.str());
                      meta[name] = field;
                    }
                    reply["meta"] = meta;
                }

                /* Register the derivation as a GC root.  !!! This
                   registers roots for jobs that we may have already
                   done. */
                if (myArgs.gcRootsDir != "") {
                    Path root = myArgs.gcRootsDir + "/" + std::string(baseNameOf(drvPath));
                    if (!pathExists(root))
                        localStore->addPermRoot(storePath, root);
                }

            }

            else if (v->type() == nAttrs)
              {
                auto attrs = nlohmann::json::array();
                StringSet ss;
                for (auto & i : v->attrs->lexicographicOrder()) {
                    std::string name(i->name);
                    if (name.find('.') != std::string::npos || name.find(' ') != std::string::npos) {
                        printError("skipping job with illegal name '%s'", name);
                        continue;
                    }
                    attrs.push_back(name);
                }
                reply["attrs"] = std::move(attrs);
            }

            else if (v->type() == nNull)
                ;

            else throw TypeError("attribute '%s' is %s, which is not supported", attrPath, showType(*v));

        } catch (EvalError & e) {
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

        writeLine(to.get(), reply.dump());

        /* If our RSS exceeds the maximum, exit. The collector will
           start a new process. */
        struct rusage r;
        getrusage(RUSAGE_SELF, &r);
        if ((size_t) r.ru_maxrss > myArgs.maxMemorySize * 1024) break;
    }

    writeLine(to.get(), "restart");
}

typedef std::function<void(EvalState & state, Bindings & autoArgs, AutoCloseFD & to, AutoCloseFD & from)>
    Processor;

/* Auto-cleanup of fork's process and fds. */
struct Proc {
    AutoCloseFD to, from;
    Pid pid;

    Proc(const Processor & proc) {
        Pipe toPipe, fromPipe;
        toPipe.create();
        fromPipe.create();
        auto p = startProcess(
            [&,
             to{std::make_shared<AutoCloseFD>(std::move(fromPipe.writeSide))},
             from{std::make_shared<AutoCloseFD>(std::move(toPipe.readSide))}
             ]()
            {
                debug("created worker process %d", getpid());
                try {
                    EvalState state(myArgs.searchPath, openStore());
                    Bindings & autoArgs = *myArgs.getAutoArgs(state);
                    proc(state, autoArgs, *to, *from);
                } catch (Error & e) {
                    nlohmann::json err;
                    auto msg = e.msg();
                    err["error"] = filterANSIEscapes(msg, true);
                    printError(msg);
                    writeLine(to->get(), err.dump());
                    // Don't forget to print it into the STDERR log, this is
                    // what's shown in the Hydra UI.
                    writeLine(to->get(), "restart");
                }
            },
            ProcessOptions { .allowVfork = false });

        to = std::move(toPipe.writeSide);
        from = std::move(fromPipe.readSide);
        pid = p;
    }

    ~Proc() { }
};

struct State
{
    std::set<std::string> todo{""};
    std::set<std::string> active;
    std::exception_ptr exc;
};

std::function<void()> collector(Sync<State> & state_, std::condition_variable & wakeup) {
    return [&]() {
        try {
            std::optional<std::unique_ptr<Proc>> proc_;

            while (true) {

                auto proc = proc_.has_value()
                    ? std::move(proc_.value())
                    : std::make_unique<Proc>(worker);

                /* Check whether the existing worker process is still there. */
                auto s = readLine(proc->from.get());
                if (s == "restart") {
                    proc_ = std::nullopt;
                    continue;
                } else if (s != "next") {
                    auto json = nlohmann::json::parse(s);
                    throw Error("worker error: %s", (std::string) json["error"]);
                }

                /* Wait for a job name to become available. */
                std::string attrPath;

                while (true) {
                    checkInterrupt();
                    auto state(state_.lock());
                    if ((state->todo.empty() && state->active.empty()) || state->exc) {
                        writeLine(proc->to.get(), "exit");
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
                writeLine(proc->to.get(), "do " + attrPath);

                /* Wait for the response. */
                auto respString = readLine(proc->from.get());
                auto response = nlohmann::json::parse(respString);

                /* Handle the response. */
                StringSet newAttrs;
                if (response.find("attrs") != response.end()) {
                    for (auto & i : response["attrs"]) {
                        auto s = (attrPath.empty() ? "" : attrPath + ".") + (std::string) i;
                        newAttrs.insert(s);
                    }
                } else {
                    auto state(state_.lock());
                    std::cout << respString << "\n" << std::flush;
                }

                proc_ = std::move(proc);

                /* Add newly discovered job names to the queue. */
                {
                    auto state(state_.lock());
                    state->active.erase(attrPath);
                    for (auto & s : newAttrs)
                        state->todo.insert(s);
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

int main(int argc, char * * argv)
{
    /* Prevent undeclared dependencies in the evaluation via
       $NIX_PATH. */
    unsetenv("NIX_PATH");

    /* We are doing the garbage collection by killing forks */
    setenv("GC_DONT_GC", "1", 1);

    return handleExceptions(argv[0], [&]() {
        initNix();
        initGC();

        myArgs.parseCmdline(argvToStrings(argc, argv));

        /* FIXME: The build hook in conjunction with import-from-derivation is causing "unexpected EOF" during eval */
        settings.builders = "";

        /* Prevent access to paths outside of the Nix search path and
           to the environment. */
        evalSettings.restrictEval = false;

        /* When building a flake, use pure evaluation (no access to
           'getEnv', 'currentSystem' etc. */
        evalSettings.pureEval = myArgs.evalMode == evalAuto ? myArgs.flake : myArgs.evalMode == evalPure;

        if (myArgs.releaseExpr == "") throw UsageError("no expression specified");

        if (myArgs.gcRootsDir == "") printMsg(lvlError, "warning: `--gc-roots-dir' not specified");

        if (myArgs.showTrace) {
            loggerSettings.showTrace.assign(true);
        }

        Sync<State> state_;

        /* Start a collector thread per worker process. */
        std::vector<std::thread> threads;
        std::condition_variable wakeup;
        for (size_t i = 0; i < myArgs.nrWorkers; i++)
            threads.emplace_back(std::thread(collector(state_, wakeup)));

        for (auto & thread : threads)
            thread.join();

        auto state(state_.lock());

        if (state->exc)
            std::rethrow_exception(state->exc);

    });
}
