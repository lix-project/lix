#include "lix/libstore/path.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/rpc.hh"
#include "lix/libutil/types-rpc.hh"
#include <algorithm>
#include <capnp/rpc-twoparty.h>
#include <chrono>
#include <cstring>
#include <future>
#include <kj/time.h>
#include <set>
#include <memory>
#include <string>
#include <tuple>
#if __APPLE__
#include <sys/time.h>
#endif

#include "lix/libstore/machines.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/pathlocks.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libstore/build-result.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libutil/strings.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libstore/types-rpc.hh"
#include "lix/libcmd/legacy.hh"
#include "lix/libutil/experimental-features.hh"
#include "lix/libutil/hash.hh"
#include "build-remote.hh"

#include "lix/libstore/build/hook-instance.capnp.h"

namespace nix {

namespace {
struct Instance final : rpc::build_remote::HookInstance::Server
{
    unsigned int maxBuildJobs;

    Instance(unsigned int maxBuildJobs) : maxBuildJobs(maxBuildJobs) {}

    kj::Promise<void> build(BuildContext context) override;
};
}

std::string escapeUri(std::string uri)
{
    std::replace(uri.begin(), uri.end(), '/', '_');
    return uri;
}

static std::string currentLoad;

static std::string makeLockFilename(const std::string & storeUri) {
    // We include 48 bytes of escaped URI to give an idea of what the lock
    // is on, then 16 bytes of hash to disambiguate.
    // This avoids issues with the escaped URI being very long and causing
    // path too long errors, while also avoiding any possibility of collision
    // caused by simple truncation.
    auto hash = hashString(HashType::SHA256, storeUri).to_string(Base::Base32, false);
    return escapeUri(storeUri).substr(0, 48) + "-" + hash.substr(0, 16);
}

static AutoCloseFD openSlotLock(const Machine & m, uint64_t slot)
{
    return openLockFile(fmt("%s/%s-%d", currentLoad, makeLockFilename(m.storeUri), slot), true);
}

static bool allSupportedLocally(Store & store, const std::set<std::string>& requiredFeatures) {
    for (auto & feature : requiredFeatures)
        if (!store.config().systemFeatures.get().count(feature)) return false;
    return true;
}

static std::tuple<bool, Machine *, AutoCloseFD> selectBestMachine(
    Machines & machines,
    const std::string & neededSystem,
    const std::set<std::string> & requiredFeatures
)
{
    bool rightType = false;
    Machine * bestMachine = nullptr;
    AutoCloseFD bestSlotLock;
    uint64_t bestLoad = 0;

    for (auto & m : machines) {
        debug("considering building on remote machine '%s'", m.storeUri);

        if (m.enabled && m.systemSupported(neededSystem) && m.allSupported(requiredFeatures)
            && m.mandatoryMet(requiredFeatures))
        {
            rightType = true;
            AutoCloseFD free;
            uint64_t load = 0;
            for (uint64_t slot = 0; slot < m.maxJobs; ++slot) {
                auto slotLock = openSlotLock(m, slot);
                if (tryLockFile(slotLock.get(), ltWrite)) {
                    if (!free) {
                        free = std::move(slotLock);
                    }
                } else {
                    ++load;
                }
            }
            if (!free) {
                continue;
            }
            bool best = false;
            if (!bestSlotLock) {
                best = true;
            } else if (load / m.speedFactor < bestLoad / bestMachine->speedFactor) {
                best = true;
            } else if (load / m.speedFactor == bestLoad / bestMachine->speedFactor) {
                if (m.speedFactor > bestMachine->speedFactor) {
                    best = true;
                } else if (m.speedFactor == bestMachine->speedFactor) {
                    if (load < bestLoad) {
                        best = true;
                    }
                }
            }
            if (best) {
                bestLoad = load;
                bestSlotLock = std::move(free);
                bestMachine = &m;
            }
        }
    }

    return {rightType, bestMachine, std::move(bestSlotLock)};
}

static void printSelectionFailureMessage(
    Verbosity level,
    const std::string_view drvstr,
    const Machines & machines,
    const std::string & neededSystem,
    const std::set<std::string> & requiredFeatures
)
{
    std::string machinesFormatted;

    for (auto & m : machines) {
        machinesFormatted += HintFmt(
                                 "\n([%s], %s, [%s], [%s])",
                                 concatStringsSep<StringSet>(", ", m.systemTypes),
                                 m.maxJobs,
                                 concatStringsSep<StringSet>(", ", m.supportedFeatures),
                                 concatStringsSep<StringSet>(", ", m.mandatoryFeatures)
        )
                                 .str();
    }

    auto error = HintFmt(
        "Failed to find a machine for remote build!\n"
        "derivation: %s\n"
        "required (system, features): (%s, [%s])\n"
        "%s available machines:\n"
        "(systems, maxjobs, supportedFeatures, mandatoryFeatures)%s",
        drvstr,
        neededSystem,
        concatStringsSep<StringSet>(", ", requiredFeatures),
        machines.size(),
        Uncolored(machinesFormatted)
    );

    printMsg(level, error.str());
}

namespace {
struct BuilderConnection
{
    AutoCloseFD slotLock;
    std::shared_ptr<Store> sshStore;
    std::string storeUri;
    Pipe logPipe;

    // start the thread that reads ssh stderr and turns it into log items.
    // this future *must* outlive sshStore, otherwise it will never finish
    std::future<void> startLogThread()
    {
        if (!logPipe.readSide) {
            return {};
        }

        logPipe.writeSide.close();

        return std::async(
            std::launch::async,
            [](AutoCloseFD logFD) {
                Activity act(*logger, lvlTalkative, actUnknown, "remote builder");
                std::vector<char> buf(4096);
                size_t currentLogLinePos = 0;
                std::string currentLogLine;

                auto flushLine = [&] {
                    act.result(resBuildLogLine, {currentLogLine});
                    currentLogLine.clear();
                    currentLogLinePos = 0;
                };

                while (true) {
                    const auto got = ::read(logFD.get(), buf.data(), buf.size());
                    if (got < 0) {
                        printError("error reading builder response: %s", strerror(errno));
                        break;
                    } else if (got == 0) {
                        if (!currentLogLine.empty()) {
                            flushLine();
                        }
                        break;
                    }

                    std::string_view data{buf.data(), size_t(got)};

                    for (auto c : data) {
                        if (c == '\r') {
                            currentLogLinePos = 0;
                        } else if (c == '\n') {
                            flushLine();
                        } else {
                            if (currentLogLinePos >= currentLogLine.size()) {
                                currentLogLine.resize(currentLogLinePos + 1);
                            }
                            currentLogLine[currentLogLinePos++] = c;
                        }
                    }
                }
            },
            std::move(logPipe.readSide)
        );
    }
};

struct AcceptedBuild final : rpc::build_remote::HookInstance::AcceptedBuild::Server
{
    ref<Store> store;
    StorePath drvPath;
    BuilderConnection builder;

    AcceptedBuild(ref<Store> store, StorePath drvPath, BuilderConnection builder)
        : store(store)
        , drvPath(drvPath)
        , builder(std::move(builder))
    {
    }

    kj::Promise<void> run(RunContext context) override;
};

enum class BuildRejected { Temporarily, Permanently };
}

static kj::Promise<Result<std::variant<BuildRejected, BuilderConnection>>> connectToBuilder(
    const ref<Store> & store,
    const std::optional<StorePath> & drvPath,
    Machines & machines,
    const unsigned int maxBuildJobs,
    const bool amWilling,
    const std::string & neededSystem,
    const std::set<std::string> & requiredFeatures
)
try {
    AutoCloseFD bestSlotLock;

    /* It would be possible to build locally after some builds clear out,
       so don't show the warning now: */
    bool couldBuildLocally = maxBuildJobs > 0
        && (neededSystem == settings.thisSystem
            || settings.extraPlatforms.get().count(neededSystem) > 0)
        && allSupportedLocally(*store, requiredFeatures);
    /* It's possible to build this locally right now: */
    bool canBuildLocally = amWilling && couldBuildLocally;

    /* Error ignored here, will be caught later */
    mkdir(currentLoad.c_str(), 0777);

    while (true) {
        bestSlotLock.reset();
        AutoCloseFD lock = openLockFile(currentLoad + "/main-lock", true);
        TRY_AWAIT(lockFileAsync(lock.get(), ltWrite));

        auto [rightType, bestMachine, slotLock] =
            selectBestMachine(machines, neededSystem, requiredFeatures);
        bestSlotLock = std::move(slotLock);

        if (!bestSlotLock) {
            if (rightType && !canBuildLocally) {
                co_return BuildRejected::Temporarily;
            } else {
                printSelectionFailureMessage(
                    couldBuildLocally ? lvlChatty : lvlWarn,
                    drvPath ? drvPath->to_string() : "<unknown>",
                    machines,
                    neededSystem,
                    requiredFeatures
                );

                co_return BuildRejected::Permanently;
            }
        }

#if __APPLE__
        futimes(bestSlotLock.get(), nullptr);
#else
        futimens(bestSlotLock.get(), nullptr);
#endif

        lock.reset();

        std::shared_ptr<Store> sshStore;
        Pipe logPipe;

        try {
            Activity act(
                *logger, lvlTalkative, actUnknown, fmt("connecting to '%s'", bestMachine->storeUri)
            );

            std::tie(sshStore, logPipe) = TRY_AWAIT(bestMachine->openStore());
            TRY_AWAIT(sshStore->connect());
            co_return BuilderConnection{
                std::move(bestSlotLock), sshStore, bestMachine->storeUri, std::move(logPipe)
            };
        } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
            std::string msg = logPipe.readSide ? chomp(drainFD(logPipe.readSide.get(), false)) : "";
            printError(
                "cannot build on '%s': %s%s",
                bestMachine->storeUri,
                e.what(),
                msg.empty() ? "" : ": " + msg
            );
            bestMachine->enabled = false;
        }
    }
} catch (...) {
    co_return result::current_exception();
}

static int main_build_remote(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        logger = makeJSONLogger(*logger);

        /* Ensure we don't get any SSH passphrase or host key popups. */
        unsetenv("DISPLAY");
        unsetenv("SSH_ASKPASS");

        /* If we ever use the common args framework, make sure to
           remove initPlugins below and initialize settings first.
        */
        if (argv.size() != 1)
            throw UsageError("called without required arguments");

        verbosity = (Verbosity) std::stoll(argv.front());

        FdSource source(STDIN_FILENO);

        /* Read the parent's settings. */
        while (readInt(source)) {
            auto name = readString(source);
            auto value = readString(source);
            settings.set(name, value);
        }

        auto maxBuildJobs = settings.maxBuildJobs;
        settings.maxBuildJobs.set("1"); // hack to make tests with local?root= work

        initPlugins();

        auto conn = aio.kj.lowLevelProvider->wrapUnixSocketFd(1);
        capnp::TwoPartyServer srv(kj::heap<Instance>(maxBuildJobs));
        srv.accept(*conn).wait(aio.kj.waitScope);
        return 0;
    }
}

kj::Promise<void> Instance::build(BuildContext context)
{
    try {
        // FIXME this does not open a daemon connection for historical reasons.
        // we may create a lot of build hook instances, and having each of them
        // also create a daemon instance is inefficient and wasteful. in future
        // versions of the build hook (where we don't need one hook process per
        // build) we should change this to using a daemon connection, ideally a
        // daemon connection provided by the parent via file descriptor passing
        auto store = TRY_AWAIT(openStore(settings.storeUri, {}, AllowDaemon::Disallow));

        /* It would be more appropriate to use $XDG_RUNTIME_DIR, since
           that gets cleared on reboot, but it wouldn't work on macOS. */
        auto currentLoadName = "/current-load";
        if (auto localStore = store.try_cast_shared<LocalFSStore>())
            currentLoad = std::string { localStore->config().stateDir } + currentLoadName;
        else
            currentLoad = settings.nixStateDir + currentLoadName;

        auto machines = getMachines();
        debug("got %d remote builders", machines.size());

        if (machines.empty()) {
            context.getResults().initResult().initGood().setDeclinePermanently();
            co_return;
        }

        auto amWilling = context.getParams().getAmWilling();
        auto neededSystem = rpc::to<std::string>(context.getParams().getNeededSystem());
        auto drvPath = from(context.getParams().getDrvPath(), *store);
        auto requiredFeatures =
            rpc::to<std::set<std::string>>(context.getParams().getRequiredFeatures());

        auto result = TRY_AWAIT(connectToBuilder(
            store, drvPath, machines, maxBuildJobs, amWilling, neededSystem, requiredFeatures
        ));

        if (auto immediateResponse = std::get_if<BuildRejected>(&result)) {
            switch (*immediateResponse) {
            case BuildRejected::Temporarily:
                context.getResults().initResult().initGood().setPostpone();
                co_return;
            case BuildRejected::Permanently:
                context.getResults().initResult().initGood().setDecline();
                co_return;
            }
        }

        auto builder = std::get_if<BuilderConnection>(&result);
        assert(builder);

        auto ac = context.getResults().initResult().initGood().initAccept();
        RPC_FILL(ac, setMachineName, builder->storeUri);
        ac.setMachine(kj::heap<AcceptedBuild>(store, drvPath, std::move(*builder)));
    } catch (...) {
        RPC_FILL(context.getResults(), initResult, std::current_exception());
    }
}

kj::Promise<void> AcceptedBuild::run(RunContext context)
{
    try {
        auto logThread = builder.startLogThread();
        KJ_DEFER({
            // drop any existing ssh connection so the log thread can exit
            builder.sshStore = nullptr;
            if (logThread.valid()) {
                logThread.get();
            }
        });

        auto & sshStore = builder.sshStore;
        auto & storeUri = builder.storeUri;

        auto inputs = rpc::to<std::set<StorePath>>(context.getParams().getInputs(), *store);
        auto wantedOutputs = rpc::to<std::set<std::string>>(context.getParams().getWantedOutputs());

        auto lockFileName = currentLoad + "/" + makeLockFilename(storeUri) + ".upload-lock";

        AutoCloseFD uploadLock = openLockFile(lockFileName, true);

        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("waiting for the upload lock to '%s'", storeUri));

            auto result = TRY_AWAIT(
                AIO().timeoutAfter(15 * kj::MINUTES, lockFileAsync(uploadLock.get(), ltWrite))
            );
            if (!result) {
                printError("somebody is hogging the upload lock for '%s', continuing...");
            }
        }

        auto substitute = settings.buildersUseSubstitutes ? Substitute : NoSubstitute;

        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("copying dependencies to '%s'", storeUri));
            TRY_AWAIT(copyPaths(*store, *sshStore, inputs, NoRepair, NoCheckSigs, substitute));
        }

        uploadLock.reset();

        auto drv = TRY_AWAIT(store->readDerivation(drvPath));

        std::optional<BuildResult> optResult;

        // If we don't know whether we are trusted (e.g. `ssh://`
        // stores), we assume we are. This is necessary for backwards
        // compat.
        bool trustedOrLegacy = ({
            std::optional trusted = TRY_AWAIT(sshStore->isTrustedClient());
            !trusted || *trusted;
        });

        // See the very large comment in `case WorkerProto::Op::BuildDerivation:` in
        // `lix/libstore/daemon.cc` that explains the trust model here.
        //
        // This condition mirrors that: that code enforces the "rules" outlined there;
        // we do the best we can given those "rules".
        if (trustedOrLegacy || drv.type().isCA())  {
            // Hijack the inputs paths of the derivation to include all
            // the paths that come from the `inputDrvs` set. We don’t do
            // that for the derivations whose `inputDrvs` is empty
            // because:
            //
            // 1. It’s not needed
            //
            // 2. Changing the `inputSrcs` set changes the associated
            //    output ids, which break CA derivations
            if (!drv.inputDrvs.empty()) {
                drv.inputSrcs = inputs;
            }
            optResult =
                TRY_AWAIT(sshStore->buildDerivation(drvPath, (const BasicDerivation &) drv));
            auto & result = *optResult;
            if (!result.success())
                throw Error(
                    "build of '%s' on '%s' failed: %s",
                    store->printStorePath(drvPath),
                    storeUri,
                    result.errorMsg
                );
        } else {
            TRY_AWAIT(copyClosure(
                *store, *sshStore, StorePathSet{drvPath}, NoRepair, NoCheckSigs, substitute
            ));
            auto res = TRY_AWAIT(sshStore->buildPathsWithResults({DerivedPath::Built{
                .drvPath = makeConstantStorePath(drvPath),
                .outputs = OutputsSpec::All{},
            }}));
            // One path to build should produce exactly one build result
            assert(res.size() == 1);
            optResult = std::move(res[0]);
        }


        StorePathSet missingPaths;
        auto outputPaths = drv.outputsAndPaths(*store);
        for (auto & [outputName, outputPath] : outputPaths) {
            if (!TRY_AWAIT(store->isValidPath(outputPath.second))) {
                missingPaths.insert(outputPath.second);
            }
        }

        if (!missingPaths.empty()) {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("copying outputs from '%s'", storeUri));
            if (auto localStore = store.try_cast_shared<LocalStore>())
                for (auto & path : missingPaths)
                    localStore->locksHeld.insert(store->printStorePath(path)); /* FIXME: ugly */
            TRY_AWAIT(
                copyPaths(*sshStore, *store, missingPaths, NoRepair, NoCheckSigs, NoSubstitute)
            );
        }

        context.getResults().initResult().setGood();
    } catch (...) {
        RPC_FILL(context.getResults(), initResult, std::current_exception());
    }
}

void registerLegacyBuildRemote() {
    LegacyCommandRegistry::add("build-remote", main_build_remote);
}

}
