#include "lix/libexpr/eval-settings.hh"
#include "lix/libfetchers/fetch-settings.hh"
#include "lix/libmain/crash-handler.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/globals.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/c-calls.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/log-format.hh"
#include "lix/libutil/config.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include "lix/libmain/loggers.hh"
#include "lix/libutil/current-process.hh"
#include "lix/libutil/terminal.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/exit.hh"

#include <algorithm>
#include <exception>
#include <iostream>

#include <cstdlib>
#include <kj/async-io.h>
#include <kj/common.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#ifdef __linux__
#include <features.h>
#endif

#include <kj/async-unix.h>
#include <openssl/crypto.h>


namespace nix {

static bool gcWarning = true;

void printGCWarning()
{
    if (!gcWarning) return;
    static bool haveWarned = false;
    if (!haveWarned) {
        haveWarned = true;
        printTaggedWarning(
            "you did not specify '--add-root'; "
            "the result might be removed by the garbage collector"
        );
    }
}


kj::Promise<Result<void>>
printMissing(ref<Store> store, const std::vector<DerivedPath> & paths, Verbosity lvl)
try {
    uint64_t downloadSize, narSize;
    StorePathSet willBuild, willSubstitute, unknown;
    TRY_AWAIT(store->queryMissing(paths, willBuild, willSubstitute, unknown, downloadSize, narSize));
    TRY_AWAIT(printMissing(store, willBuild, willSubstitute, unknown, downloadSize, narSize, lvl));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> printMissing(ref<Store> store, const StorePathSet & willBuild,
    const StorePathSet & willSubstitute, const StorePathSet & unknown,
    uint64_t downloadSize, uint64_t narSize, Verbosity lvl)
try {
    if (!willBuild.empty()) {
        if (willBuild.size() == 1)
            printMsg(lvl, "this derivation will be built:");
        else
            printMsg(lvl, "these %d derivations will be built:", willBuild.size());
        auto sorted = TRY_AWAIT(store->topoSortPaths(willBuild));
        reverse(sorted.begin(), sorted.end());
        for (auto & i : sorted)
            printMsg(lvl, "  %s", Uncolored(store->printStorePath(i)));
    }

    if (!willSubstitute.empty()) {
        const float downloadSizeMiB = downloadSize / (1024.f * 1024.f);
        const float narSizeMiB = narSize / (1024.f * 1024.f);
        if (willSubstitute.size() == 1) {
            printMsg(lvl, "this path will be fetched (%.2f MiB download, %.2f MiB unpacked):",
                downloadSizeMiB,
                narSizeMiB);
        } else {
            printMsg(lvl, "these %d paths will be fetched (%.2f MiB download, %.2f MiB unpacked):",
                willSubstitute.size(),
                downloadSizeMiB,
                narSizeMiB);
        }
        std::vector<const StorePath *> willSubstituteSorted = {};
        std::for_each(willSubstitute.begin(), willSubstitute.end(),
                   [&](const StorePath &p) { willSubstituteSorted.push_back(&p); });
        // NOTE: this sort uses a total order, so the iteration over pointers is not an issue
        // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order)
        std::sort(willSubstituteSorted.begin(), willSubstituteSorted.end(),
                  [](const StorePath *lhs, const StorePath *rhs) {
                    if (lhs->name() == rhs->name())
                      return lhs->to_string() < rhs->to_string();
                    else
                      return lhs->name() < rhs->name();
                  });
        for (auto p : willSubstituteSorted)
            printMsg(lvl, "  %s", Uncolored(store->printStorePath(*p)));
    }

    if (!unknown.empty()) {
        printMsg(lvl, "don't know how to build these paths%s:",
                (settings.readOnlyMode ? " (may be caused by read-only store access)" : ""));
        for (auto & i : unknown)
            printMsg(lvl, "  %s", Uncolored(store->printStorePath(i)));
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


std::string getArg(const std::string & opt,
    Strings::iterator & i, const Strings::iterator & end)
{
    ++i;
    if (i == end) throw UsageError("'%1%' requires an argument", opt);
    return *i;
}

static void sigHandler(int signo) { }


void initNix()
{
    kj::UnixEventPort::setReservedSignal(KJ_RESERVED_SIGNAL);

    registerCrashHandler();

    // libutil
    GlobalConfig::registerGlobalConfig(loggerSettings);
    GlobalConfig::registerGlobalConfig(featureSettings);
    GlobalConfig::registerGlobalConfig(archiveSettings);
    // libfetchers
    GlobalConfig::registerGlobalConfig(fetchSettings);
    // libexpr
    GlobalConfig::registerGlobalConfig(evalSettings);

    initLibStore();

    startSignalHandlerThread();

    /* Reset SIGCHLD to its default. */
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    act.sa_handler = SIG_DFL;
    if (sigaction(SIGCHLD, &act, 0))
        throw SysError("resetting SIGCHLD");

    /* Install a dummy INTERRUPT_NOTIFY_SIGNAL handler for use with pthread_kill(). */
    act.sa_handler = sigHandler;
    if (sigaction(INTERRUPT_NOTIFY_SIGNAL, &act, 0)) {
        throw SysError("handling interrupt notify signal %i", INTERRUPT_NOTIFY_SIGNAL);
    }

#if __APPLE__
    /* HACK: on darwin, we need can’t use sigprocmask with SIGWINCH.
     * Instead, add a dummy sigaction handler, and signalHandlerThread
     * can handle the rest. */
    act.sa_handler = sigHandler;
    if (sigaction(SIGWINCH, &act, 0)) throw SysError("handling SIGWINCH");

    /* Disable SA_RESTART for interrupts, so that system calls on this thread
     * error with EINTR like they do on Linux.
     * Most signals on BSD systems default to SA_RESTART on, but Nix
     * expects EINTR from syscalls to properly exit. */
    act.sa_handler = SIG_DFL;
    if (sigaction(SIGINT, &act, 0)) throw SysError("handling SIGINT");
    if (sigaction(SIGTERM, &act, 0)) throw SysError("handling SIGTERM");
    if (sigaction(SIGHUP, &act, 0)) throw SysError("handling SIGHUP");
    if (sigaction(SIGPIPE, &act, 0)) throw SysError("handling SIGPIPE");
    if (sigaction(SIGQUIT, &act, 0)) throw SysError("handling SIGQUIT");
    if (sigaction(SIGTRAP, &act, 0)) throw SysError("handling SIGTRAP");
#endif

    /* Register a SIGSEGV handler to detect stack overflows.
       Why not initLibExpr()? initGC() is essentially that, but
       detectStackOverflow is not an instance of the init function concept, as
       it may have to be invoked more than once per process. */
    detectStackOverflow();

    /* There is no privacy in the Nix system ;-)  At least not for
       now.  In particular, store objects should be readable by
       everybody. */
    umask(0022);
}


LegacyArgs::LegacyArgs(AsyncIoRoot & aio, const std::string & programName,
    std::function<bool(Strings::iterator & arg, const Strings::iterator & end)> parseArg)
    : MixCommonArgs(programName), aio_(aio), parseArg(parseArg)
{
    addFlag({
        .longName = "no-build-output",
        .shortName = 'Q',
        .description = "Do not show build output.",
        .handler = {[&]() { setLogFormat(loggerSettings.logFormat.get().withoutLogs()); }},
    });

    addFlag({
        .longName = "keep-failed",
        .shortName ='K',
        .description = "Keep temporary directories of failed builds.",
        .handler = {[&]() { settings.keepFailed.override(true); }},
    });

    addFlag({
        .longName = "keep-going",
        .shortName ='k',
        .description = "Keep going after a build fails.",
        .handler = {[&]() { settings.keepGoing.override(true); }},
    });

    addFlag({
        .longName = "fallback",
        .description = "Build from source if substitution fails.",
        .handler = {[&]() { settings.tryFallback.override(true); }},
    });

    auto intSettingAlias = [&](char shortName, const std::string & longName,
        const std::string & description, const std::string & dest)
    {
        addFlag({
            .longName = longName,
            .shortName = shortName,
            .description = description,
            .labels = {"n"},
            .handler = {[=](std::string s) {
                auto n = string2IntWithUnitPrefix<uint64_t>(s);
                settings.set(dest, std::to_string(n));
            }}
        });
    };

    intSettingAlias(0, "cores", "Maximum number of CPU cores to use inside a build.", "cores");
    intSettingAlias(0, "max-silent-time", "Number of seconds of silence before a build is killed.", "max-silent-time");
    intSettingAlias(0, "timeout", "Number of seconds before a build is killed.", "timeout");

    addFlag({
        .longName = "readonly-mode",
        .description = "Do not write to the Nix store.",
        .handler = {&settings.readOnlyMode, true},
    });

    addFlag({
        .longName = "no-gc-warning",
        .description = "Disable warnings about not using `--add-root`.",
        .handler = {&gcWarning, false},
    });

    addFlag({
        .longName = "store",
        .description = "The URL of the Nix store to use.",
        .labels = {"store-uri"},
        .handler = {[&](std::string storeUri) { settings.storeUri.override(storeUri); }},
    });
}


bool LegacyArgs::processFlag(Strings::iterator & pos, Strings::iterator end)
{
    if (MixCommonArgs::processFlag(pos, end)) return true;
    bool res = parseArg(pos, end);
    if (res) ++pos;
    return res;
}


bool LegacyArgs::processArgs(const Strings & args, bool finish)
{
    if (args.empty()) return true;
    assert(args.size() == 1);
    Strings ss(args);
    auto pos = ss.begin();
    if (!parseArg(pos, ss.end()))
        throw UsageError("unexpected argument '%1%'", args.front());
    return true;
}


void printVersion(const std::string & programName)
{
    std::cout << fmt("%1% (Lix, like Nix) %2%", programName, nixVersion) << std::endl;
    Strings cfg;
#if HAVE_BOEHMGC
    cfg.push_back("gc");
#endif
    cfg.push_back("signed-caches");
    std::cout << "System type: " << settings.thisSystem << "\n";
    std::cout << "Additional system types: " << concatStringsSep(", ", settings.extraPlatforms.get()) << "\n";
    std::cout << "Features: " << concatStringsSep(", ", cfg) << "\n";
    std::cout << "System configuration file: " << settings.nixConfDir + "/nix.conf" << "\n";
    std::cout << "User configuration files: " <<
        concatStringsSep(":", settings.nixUserConfFiles)
        << "\n";
    std::cout << "Store directory: " << settings.nixStore << "\n";
    std::cout << "State directory: " << settings.nixStateDir << "\n";
    std::cout << "Data directory: " << settings.nixDataDir << "\n";
    throw Exit();
}


void showManPage(const std::string & name)
{
    restoreProcessContext();
    (void) sys::setenv("MANPATH", settings.nixManDir, 1);
    execlp("man", "man", requireCString(name).asCStr(), nullptr);
    throw SysError("command 'man %1%' failed", name.c_str());
}

int handleExceptions(const std::string & programName, std::function<int()> fun)
{
    ReceiveInterrupts receiveInterrupts; // FIXME: need better place for this

    /* Lix command line is not yet stabilized.
     * Explain that it is experimental and reserved for custom subcommands for now.
     * */
    bool onlyForSubcommands = baseNameOf(programName) == "lix";

    try {
        return fun();
    } catch (Exit & e) {
        return e.status;
    } catch (UsageError & e) {
        logError(e.info());
        if (onlyForSubcommands)
            printError("'%1%' is reserved for external subcommands, is your subcommand available in the PATH?", programName);
        else
            printError("Try '%1%' for more information.", programName + " --help");
        return 1;
    } catch (BaseError & e) {
        logError(e.info());
        return e.info().status;
    } catch (const std::bad_alloc & e) {
        printError(ANSI_RED "error:" ANSI_NORMAL " out of memory");
        return 1;
    }
    // Deliberately do not catch random std exceptions! We have a nice
    // std::terminate handler for those, and if we allow it to crash hard, it
    // will produce better backtraces and more useful core dumps.
    //
    // We want to crash on those regardless, but omitting the handling is
    // better than including it for that.
    //
    // If we catch them, we will land in terminate in phase 2 of unwind with
    // all the frames between the throw and the catch already cleaned up,
    // whereas if there is no handler (or it falls into a noexcept) it will
    // terminate immediately at the end of phase 1 unwind while still having a
    // stack and with no stack variables destroyed.

    return 0;
}

static std::pair<RunningHelper, AutoCloseFD> startPager()
{
    if (!isOutputARealTerminal(StandardOutputStream::Stdout)) {
        return {};
    }
    char * pager = getenv("NIX_PAGER");
    if (!pager) pager = getenv("PAGER");
    if (pager && ((std::string) pager == "" || (std::string) pager == "cat")) {
        return {};
    }

    Pipe toPager;
    toPager.create();

    auto helper = runHelper(
        "run-pager",
        {
            .args = pager ? Strings{pager} : Strings{},
            .redirections = {{.dup = STDIN_FILENO, .from = toPager.readSide.get()}},
        }
    );

    return {std::move(helper), std::move(toPager.writeSide)};
}

void withPager(kj::Function<void(Pager &)> fn)
{
    struct PagerImpl : Pager
    {
        int to;

        explicit PagerImpl(int to) : to(to) {}

        Pager & operator<<(std::string_view data) override
        {
            writeFull(to, data);
            return *this;
        }
    };

    logger->pause();
    KJ_DEFER(logger->resume());

    auto [pagerProc, pagerPipe] = startPager();
    KJ_DEFER({
        if (pagerProc) {
            pagerProc.kill();
        }
    });

    PagerImpl pager{pagerProc ? pagerPipe.get() : STDOUT_FILENO};
    fn(pager);
    pagerPipe.close();
    if (pagerProc) {
        pagerProc.waitAndCheck();
    }
}

PrintFreed::~PrintFreed()
{
    // When in dry-run mode, print the paths on stdout
    if (action == GCOptions::gcReturnLive || action == GCOptions::gcReturnDead) {
        for (auto & i : results.paths) {
            logger->cout("%s", i);
        };
    }

    switch (action) {
    case GCOptions::gcReturnLive: {
        notice("%1% store paths would be kept\n", results.paths.size());
        break;
    }
    case GCOptions::gcReturnDead: {
        notice("%1% store paths would be deleted\n", results.paths.size());
        break;
    }
    case GCOptions::gcDeleteDead:
    case GCOptions::gcDeleteSpecific:
    case GCOptions::gcTryDeleteSpecific: {
        notice(
            "%1% store paths deleted, %2% freed\n",
            results.paths.size(),
            showBytes(results.bytesFreed)
        );
        break;
    }
    }
}
}
