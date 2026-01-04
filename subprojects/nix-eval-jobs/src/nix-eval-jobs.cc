#include <chrono>
#include <lix/config.h> // IWYU pragma: keep

#include <lix/libexpr/eval-settings.hh>
#include <lix/libmain/shared.hh>
#include <lix/libutil/async.hh>
#include <lix/libutil/async-collect.hh>
#include <lix/libutil/async-semaphore.hh>
#include <lix/libutil/sync.hh>
#include <lix/libexpr/eval.hh>
#include <lix/libutil/json.hh>
#include <lix/libutil/signals.hh>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lix/libexpr/attr-set.hh>
#include <lix/libutil/config.hh>
#include <lix/libutil/error.hh>
#include <lix/libstore/globals.hh>
#include <lix/libutil/current-process.hh>
#include <lix/libutil/finally.hh>
#include <lix/libutil/logging.hh>
#include <lix/libutil/terminal.hh>
#include <lix/libutil/ref.hh>
#include <lix/libstore/store-api.hh>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "constituents.hh"
#include "eval-args.hh"
#include "buffered-io.hh"
#include "worker.hh"

using namespace nix;

static constexpr int NEJ_FROM_WORKER_FD = 3;
static constexpr int NEJ_TO_WORKER_FD = 4;

std::string joinAttrPath(const JSON &attrPath) {
    std::string joined;
    for (const auto &element : attrPath) {
        if (!joined.empty()) {
            joined += '.';
        }
        joined += element.get<std::string>();
    }
    return joined;
}

class Collector {
    struct Do {
        JSON attrPath;
    };
    struct Exit {};
    using Request = std::variant<Do, Exit>;

    struct Next {};
    struct JsonResponse {
        JSON json;
    };
    struct Restart {};
    using Response = std::variant<Next, JsonResponse, Restart>;

    RunningProgram child;
    std::optional<AsyncFdIoStream> to;
    std::optional<AsyncLineReader> from;

    Strings workerCmdline;

    void startWorker() {
        const auto self = [] {
            auto tmp = getSelfExe();
            if (!tmp) {
                throw Error("can't locate the nix-eval-jobs binary!");
            }
            return *tmp;
        }();

        Pipe toPipe, fromPipe;
        toPipe.create();
        fromPipe.create();

        RunOptions options{
            .program = self,
            .argv0 = "nix-eval-jobs",
            .args = workerCmdline,
            .redirections =
                {
                    {.dup = NEJ_FROM_WORKER_FD, .from = fromPipe.writeSide.get()},
                    {.dup = NEJ_TO_WORKER_FD, .from = toPipe.readSide.get()},
                },
        };

        child = runProgram2(options);
        to.emplace(std::move(toPipe.writeSide));
        from.emplace(std::move(fromPipe.readSide));
    }

    kj::Promise<Result<void>> waitForWorkerReady()
    try {
        assert(child);
        std::visit(overloaded{
            [](const Next &) {},
            [](const JsonResponse &response) {
                throw Error("worker error: %s", (std::string) response.json["error"]);
            },
            [&](const Restart &) {
                (void) child.wait();
                to.reset();
                from.reset();
            },
        }, LIX_TRY_AWAIT(readResponse("checking worker process")));
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> makeWorkerReady()
    try {
        if (child) {
            LIX_TRY_AWAIT(waitForWorkerReady());
        }
        if (!child) {
            startWorker();
            LIX_TRY_AWAIT(waitForWorkerReady());
        }
        if (!child) {
            throw Error("worker exited immediately");
        }
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<Response>> readResponse(std::string_view msg)
    try {
        assert(from);
        auto line = LIX_TRY_AWAIT(from->readLine());
        if (!line) {
            handleBrokenPipe(msg);
        } else if (line == "next") {
            co_return Next{};
        } else if (line == "restart") {
            co_return Restart{};
        } else {
            try {
                co_return JsonResponse{JSON::parse(*line)};
            } catch (const json::JSONError &e) {
                throw Error(
                    "Received invalid JSON from worker: %s\n json: '%s'",
                    e.what(), *line);
            }
        }
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> writeRequest(Request request)
    try {
        assert(to);
        auto line = std::visit(overloaded{
            [](const Do &request) {
                return fmt("do %s\n", request.attrPath.dump());
            },
            [](const Exit &) {
                return std::string{"exit\n"};
            },
        }, request);
        try {
            LIX_TRY_AWAIT(to->writeFull(line.data(), line.size()));
        } catch (SysError &err) {
            auto msg = std::visit(overloaded{
                [](const Do &request) {
                    return fmt("sending attrPath '%s'", joinAttrPath(request.attrPath));
                },
                [](const Exit &) {
                    return std::string{"sending exit"};
                },
            }, request);
            handleBrokenPipe(msg);
        }
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    [[noreturn]] void handleBrokenPipe(std::string_view msg) {
        int status = child.wait();
        to.reset();
        from.reset();
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                // On the user hitting Ctrl-C, both the worker and the coordinator will receive the signal.
                // When the worker is interrupted, it will "unexpectedly" exit successfully.
                // Check whether the coordinator was interrupted as well, and don't show an ugly error in this case.
                checkInterrupt();
                // Maybe the coordinator noticed the broken pipe before its own interrupt.
                // Wait for a bit and try again.
                std::this_thread::sleep_for(std::chrono::seconds(1));
                checkInterrupt();
                // No, the coordinator was not interrupted, possibly the signal was sent manually to the worker.
                // Show the error in this case.
            } else if (WEXITSTATUS(status) == 1) {
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
            case SIGSEGV:
                throw Error(
                    "while %s, evaluation worker got killed by SIGSEGV, "
                    "(possible infinite recursion)",
                    msg);
            default:
                throw Error(
                    "while %s, evaluation worker got killed by signal %d (%s)",
                    msg, WTERMSIG(status), strsignal(WTERMSIG(status)));
            }
        } else {
            // WIFSTOPPED and WIFCONTINUED should not happen, as neither WUNTRACED nor WCONTINUED are passed
            throw Error("while %s, waitpid for evaluation worker returned unexpected status: %d", msg, status);
        }
    }

public:
    Collector(Strings cmdline) : workerCmdline{std::move(cmdline)} {
        workerCmdline.push_front("--worker");
    }

    ~Collector() {
        if (child) {
            child.kill();
        }
    }

    kj::Promise<Result<JSON>> evaluate(JSON attrPath)
    try {
        LIX_TRY_AWAIT(makeWorkerReady());
        LIX_TRY_AWAIT(writeRequest(Do{attrPath}));
        co_return std::visit(overloaded{
            [](const Next &) -> JSON {
                throw Error("unexpected response from worker: next");
            },
            [](const JsonResponse &response) {
                return response.json;
            },
            [](const Restart &) -> JSON {
                throw Error("unexpected response from worker: restart");
            },
        }, LIX_TRY_AWAIT(readResponse(fmt("reading result for attrPath '%s'", joinAttrPath(attrPath)))));
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> exit()
    try {
        if (child) {
            LIX_TRY_AWAIT(waitForWorkerReady());
        }
        if (child) {
            LIX_TRY_AWAIT(writeRequest(Exit{}));
            // The worker will print "restart" when exiting cleanly, even if due to an explicit exit request.
            LIX_TRY_AWAIT(waitForWorkerReady());
        }
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }
};

class Coordinator {
    AsyncSemaphore semaphore;
    kj::Array<Collector> workers;
    std::vector<Collector *> idleWorkers;

    kj::Promise<Result<void>> evaluateRecursively(JSON attrPath, std::map<std::string, JSON> &jobs)
    try {
        JSON response;
        {
            auto _token = co_await semaphore.acquire();
            auto *worker = idleWorkers.back();
            idleWorkers.pop_back();
            Finally _returnWorker{[&]() {
                idleWorkers.push_back(worker);
            }};
            response = LIX_TRY_AWAIT(worker->evaluate(attrPath));
        }

        std::vector<JSON> newAttrs;
        if (response.find("attrs") != response.end()) {
            for (auto &i : response["attrs"]) {
                JSON newAttr = JSON(response["attrPath"]);
                newAttr.emplace_back(i);
                newAttrs.push_back(newAttr);
            }
        } else {
            jobs.insert_or_assign(response["attr"], response);
            if (nix::settings.readOnlyMode) {
                response.erase("namedConstituents");
                response.erase("constituents");
            }
            auto named = response.find("namedConstituents");
            if (named == response.end() || named->empty()) {
                response.erase("namedConstituents");
                nix::logger->writeToStdout(response.dump());
            }
        }

        LIX_TRY_AWAIT(asyncSpread(newAttrs, [&](const JSON &newAttr) {
            return evaluateRecursively(newAttr, jobs);
        }));

        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

public:
    Coordinator(size_t nrWorkers, const Strings &cmdline)
        : semaphore{static_cast<unsigned>(nrWorkers)} {
        if (nrWorkers >= std::numeric_limits<unsigned>::max() - 1) {
            throw Error("nix-eval-jobs cannot handle %d workers, please choose a reasonable number");
        }

        auto builder = kj::heapArrayBuilder<Collector>(nrWorkers);
        for (unsigned i = 0; i < nrWorkers; ++i) {
            auto worker = &builder.add(cmdline);
            idleWorkers.push_back(worker);
        }
        workers = builder.finish();
    }

    kj::Promise<Result<std::map<std::string, JSON>>> run()
    try {
        std::map<std::string, JSON> jobs;
        LIX_TRY_AWAIT(evaluateRecursively(JSON::array(), jobs));
        for (auto &worker : workers) {
            LIX_TRY_AWAIT(worker.exit());
        }
        co_return jobs;
    } catch (...) {
        co_return result::current_exception();
    }
};

int main(int argc, char **argv) {
    return handleExceptions(argv[0], [&]() {
        initNix();
        initLibExpr();

        nix::AsyncIoRoot aio;
        MyArgs myArgs(aio);

        myArgs.parseArgs(argv, argc);

        /* Set no-instantiate mode if requested (makes evaluation faster) */
        if (myArgs.noInstantiate) {
            nix::settings.readOnlyMode = true;
            if (myArgs.constituents) {
                throw UsageError("--no-instantiate and --constituents are mutually exclusive");
            }
            if (myArgs.checkCacheStatus) {
                throw UsageError("--no-instantiate and --check-cache-status are mutually exclusive");
            }
        }

        /* When building a flake, use pure evaluation (no access to
           'getEnv', 'currentSystem' etc. */
        if (myArgs.impure) {
            evalSettings.pureEval.override(false);
        } else if (myArgs.flake) {
            evalSettings.pureEval.override(true);
        }

        if (myArgs.releaseExpr == "")
            throw UsageError("no expression specified");

        if (myArgs.gcRootsDir == "") {
            printMsg(lvlError, "warning: `--gc-roots-dir' not specified");
        } else {
            myArgs.gcRootsDir = std::filesystem::absolute(myArgs.gcRootsDir);
        }

        if (myArgs.showTrace) {
            loggerSettings.showTrace.override(true);
        }

        if (myArgs.worker) {
            AutoCloseFD fromCoordinator{NEJ_TO_WORKER_FD};
            AutoCloseFD toCoordinator{NEJ_FROM_WORKER_FD};
            worker(toCoordinator, fromCoordinator, myArgs);
            return 0;
        }

        Coordinator coordinator{myArgs.nrWorkers, myArgs.cmdline};
        auto jobs = aio.blockOn(coordinator.run());

        if (myArgs.constituents) {
            auto store = aio.blockOn(myArgs.evalStoreUrl
                                         ? nix::openStore(*myArgs.evalStoreUrl)
                                         : nix::openStore());
            std::visit(
                nix::overloaded{
                    [&](const std::vector<AggregateJob> &namedConstituents) {
                        rewriteAggregates(jobs, namedConstituents, store,
                                          myArgs.gcRootsDir, aio);
                    },
                    [&](const DependencyCycle &e) {
                        printError(
                            "Found dependency cycle between jobs '%s' and '%s'",
                            e.a, e.b);
                        jobs[e.a]["error"] = e.message();
                        jobs[e.b]["error"] = e.message();

                        nix::logger->writeToStdout(jobs[e.a].dump());
                        nix::logger->writeToStdout(jobs[e.b].dump());

                        for (const auto &jobName : e.remainingAggregates) {
                            jobs[jobName]["error"] =
                                "Skipping aggregate because of a dependency "
                                "cycle";
                            nix::logger->writeToStdout(jobs[jobName].dump());
                        }
                    },
                },
                resolveNamedConstituents(jobs));
        }

        return 0;
    });
}
