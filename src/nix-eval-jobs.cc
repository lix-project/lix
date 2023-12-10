#include <map>
#include <sys/signal.h>
#include <thread>
#include <condition_variable>
#include <filesystem>

#include <nix/config.h>
#include <nix/eval-settings.hh>
#include <nix/common-eval-args.hh>
#include <nix/args/root.hh>
#include <nix/shared.hh>
#include <nix/sync.hh>
#include <nix/eval.hh>
#include <nix/get-drvs.hh>
#include <nix/value-to-json.hh>
#include <nix/local-fs-store.hh>
#include <nix/signals.hh>
#include <nix/terminal.hh>
#include <sys/wait.h>

#include "eval-args.hh"
#include "drv.hh"
#include "buffered-io.hh"
#include "worker.hh"

#include <nlohmann/json.hpp>

using namespace nix;
using namespace nlohmann;

static MyArgs myArgs;

typedef std::function<void(ref<EvalState> state, Bindings &autoArgs,
                           AutoCloseFD &to, AutoCloseFD &from, MyArgs &args)>
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
                    proc(ref<EvalState>(state), autoArgs, *to, *from, myArgs);
                } catch (Error &e) {
                    nlohmann::json err;
                    auto msg = e.msg();
                    err["error"] = nix::filterANSIEscapes(msg, true);
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
    // we already took the process status from Proc, no
    // need to wait for it again to avoid error messages
    pid_t pid = proc.pid.release();
    while (1) {
        int rc = waitpid(pid, nullptr, WNOHANG);
        if (rc == 0) {
            kill(pid, SIGKILL);
            throw Error("BUG: worker pipe closed but worker still running?");
        } else if (rc == -1) {
            kill(pid, SIGKILL);
            throw Error("BUG: waitpid waiting for worker failed: %s",
                        strerror(errno));
        } else {
            if (WIFEXITED(rc)) {
                throw Error("evaluation worker exited with %d",
                            WEXITSTATUS(rc));
            } else if (WIFSIGNALED(rc)) {
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

void collector(Sync<State> &state_, std::condition_variable &wakeup) {
    try {
        std::optional<std::unique_ptr<Proc>> proc_;
        std::optional<std::unique_ptr<LineReader>> fromReader_;

        while (true) {
            if (!proc_.has_value()) {
                proc_ = std::make_unique<Proc>(worker);
                fromReader_ =
                    std::make_unique<LineReader>(proc_.value()->from.release());
            }
            auto proc = std::move(proc_.value());
            auto fromReader = std::move(fromReader_.value());

            /* Check whether the existing worker process is still there. */
            auto s = fromReader->readLine();
            if (s.empty()) {
                handleBrokenWorkerPipe(*proc.get());
            } else if (s == "restart") {
                proc_ = std::nullopt;
                fromReader_ = std::nullopt;
                continue;
            } else if (s != "next") {
                try {
                    auto json = json::parse(s);
                    throw Error("worker error: %s", (std::string)json["error"]);
                } catch (const json::exception &e) {
                    throw Error(
                        "Received invalid JSON from worker: %s\n json: '%s'",
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
            if (respString.empty()) {
                handleBrokenWorkerPipe(*proc.get());
            }
            json response;
            try {
                response = json::parse(respString);
            } catch (const json::exception &e) {
                throw Error(
                    "Received invalid JSON from worker: %s\n json: '%s'",
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

        myArgs.parseArgs(argv, argc);

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
        for (size_t i = 0; i < myArgs.nrWorkers; i++) {
            threads.emplace_back(collector, std::ref(state_), std::ref(wakeup));
        }

        for (auto &thread : threads)
            thread.join();

        auto state(state_.lock());

        if (state->exc)
            std::rethrow_exception(state->exc);
    });
}
