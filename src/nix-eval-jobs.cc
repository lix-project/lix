#include <nix/config.h> // IWYU pragma: keep

#include <nix/eval-settings.hh>
#include <nix/shared.hh>
#include <nix/sync.hh>
#include <nix/eval.hh>
#include <nix/signals.hh>
#include <nix/terminal.hh>
#include <sys/wait.h>
#include <nlohmann/json.hpp>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <nix/attr-set.hh>
#include <nix/config.hh>
#include <nix/error.hh>
#include <nix/file-descriptor.hh>
#include <nix/globals.hh>
#include <nix/logging.hh>
#include <nlohmann/detail/iterators/iter_impl.hpp>
#include <nlohmann/detail/json_ref.hpp>
#include <nlohmann/json_fwd.hpp>
#include <nix/processes.hh>
#include <nix/ref.hh>
#include <nix/store-api.hh>
#include <map>
#include <thread>
#include <condition_variable>
#include <filesystem>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "eval-args.hh"
#include "buffered-io.hh"
#include "worker.hh"

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

void handleBrokenWorkerPipe(Proc &proc, std::string_view msg) {
    // we already took the process status from Proc, no
    // need to wait for it again to avoid error messages
    pid_t pid = proc.pid.release();
    while (1) {
        int status;
        int rc = waitpid(pid, &status, WNOHANG);
        if (rc == 0) {
            kill(pid, SIGKILL);
            throw Error("BUG: while %s, worker pipe got closed but evaluation "
                        "worker still running?",
                        msg);
        } else if (rc == -1) {
            kill(pid, SIGKILL);
            throw Error(
                "BUG: while %s, waitpid for evaluation worker failed: %s", msg,
                strerror(errno));
        } else {
            if (WIFEXITED(status)) {
                if (WEXITSTATUS(status) == 1) {
                    throw Error(
                        "while %s, evaluation worker exited with exit code 1, "
                        "(possible infinite recursion)",
                        msg);
                }
                throw Error("while %s, evaluation worker exited with %d", msg,
                            WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                switch (WTERMSIG(status)) {
                case SIGKILL:
                    throw Error(
                        "while %s, evaluation worker got killed by SIGKILL, "
                        "maybe "
                        "memory limit reached?",
                        msg);
                    break;
#ifdef __APPLE__
                case SIGBUS:
                    throw Error(
                        "while %s, evaluation worker got killed by SIGBUS, "
                        "(possible infinite recursion)",
                        msg);
                    break;
#else
                case SIGSEGV:
                    throw Error(
                        "while %s, evaluation worker got killed by SIGSEGV, "
                        "(possible infinite recursion)",
                        msg);
#endif
                }
                throw Error(
                    "while %s, evaluation worker got killed by signal %d (%s)",
                    msg, WTERMSIG(status), strsignal(WTERMSIG(status)));
            } // else ignore WIFSTOPPED and WIFCONTINUED
        }
    }
}

std::string joinAttrPath(json &attrPath) {
    std::string joined;
    for (auto &element : attrPath) {
        if (!joined.empty()) {
            joined += '.';
        }
        joined += element.get<std::string>();
    }
    return joined;
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
                handleBrokenWorkerPipe(*proc.get(), "checking worker process");
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
                        handleBrokenWorkerPipe(*proc.get(), "sending exit");
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
                auto msg = "sending attrPath '" + joinAttrPath(attrPath) + "'";
                handleBrokenWorkerPipe(*proc.get(), msg);
            }

            /* Wait for the response. */
            auto respString = fromReader->readLine();
            if (respString.empty()) {
                auto msg = "reading result for attrPath '" +
                           joinAttrPath(attrPath) + "'";
                handleBrokenWorkerPipe(*proc.get(), msg);
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
