#include "lix/libstore/path.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/c-calls.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/logging-rpc.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/rpc.hh"
#include "lix/libutil/types-rpc.hh" // IWYU pragma: keep
#include "lix/libutil/types.hh"
#include "lix/lix-rs/utils.hh"
#include <algorithm>
#include <capnp/rpc-twoparty.h>
#include <cstring>
#include <exception>
#include <kj/async.h>
#include <kj/time.h>
#include <set>
#include <memory>
#include <string>
#include <tuple>
#include <sys/time.h>

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
#include "lix/lix-rs/main.gen.hh"
#include "build-remote.hh"

#include "lix/libstore/build/hook-instance.capnp.h"

namespace nix {

namespace {
struct Instance final : rpc::build_remote::HookInstance::Server
{
    bool initialized = false, used = false;

    kj::Promise<void> init(InitContext context) override;

    kj::Promise<Result<void>> buildImpl(BuildContext context);
    kj::Promise<void> build(BuildContext context) override;
};
}

std::string escapeUri(std::string uri)
{
    std::replace(uri.begin(), uri.end(), '/', '_');
    return uri;
}

static std::string makeLockFilename(const std::string & storeUri) {
    // We include 48 bytes of escaped URI to give an idea of what the lock
    // is on, then 16 bytes of hash to disambiguate.
    // This avoids issues with the escaped URI being very long and causing
    // path too long errors, while also avoiding any possibility of collision
    // caused by simple truncation.
    auto hash = base32Encode(hashString(HashType::SHA256, storeUri));
    return escapeUri(storeUri).substr(0, 48) + "-" + hash.substr(0, 16);
}

namespace {
struct BuilderConnection
{
    std::shared_ptr<Store> sshStore;
    std::string storeUri;
};

struct AcceptedBuild final : rpc::build_remote::HookInstance::AcceptedBuild::Server
{
    ref<Store> store;
    StorePath drvPath;
    BuilderConnection builder;
    bool used = false;
    Logger * oldLogger = nullptr;

    AcceptedBuild(ref<Store> store, StorePath drvPath, BuilderConnection builder)
        : store(store)
        , drvPath(drvPath)
        , builder(std::move(builder))
    {
    }

    ~AcceptedBuild()
    {
        // revert to previous logger as rpc logger will go away during teardown.
        // dangling pointers tend to have deleterious effects on program health.
        if (oldLogger) {
            std::swap(logger, oldLogger);
            delete oldLogger;
        }
    }

    kj::Promise<Result<void>> runImpl(RunContext context);
    kj::Promise<void> run(RunContext context) override;
};
}

static int main_build_remote(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        /* Ensure we don't get any SSH passphrase or host key popups. */
        unsetenv("DISPLAY");
        unsetenv("SSH_ASKPASS");

        /* If we ever use the common args framework, make sure to
           remove initPlugins below and initialize settings first.
        */
        if (argv.size() != 1)
            throw UsageError("called without required arguments");

        setVerbosity((Verbosity) std::stoll(argv.front()));

        auto conn = aio.kj.lowLevelProvider->wrapUnixSocketFd(1);
        capnp::TwoPartyServer srv(kj::heap<Instance>());
        srv.accept(*conn, 1).wait(aio.kj.waitScope);
        return 0;
    }
}

kj::Promise<void> Instance::init(InitContext context)
{
    return RPC_IMPL({
        if (initialized) {
            throw Error("build hook can only be initialized once");
        }

        logger = rpc::log::makeRpcLoggerClient(context.getParams().getLogger());

        /* Read the parent's settings. */
        for (const auto & [name, value] : rpc::to<StringMap>(context.getParams().getSettings())) {
            globalConfig.set(name, value);
        }

        settings.maxBuildJobs.set("1"); // hack to make tests with local?root= work

        initPlugins();

        initialized = true;
    });
}

kj::Promise<Result<void>> Instance::buildImpl(BuildContext context)
try {
    if (!initialized) {
        throw Error("build hook not fully initialized");
    }

    // FIXME this does not open a daemon connection for historical reasons.
    // we may create a lot of build hook instances, and having each of them
    // also create a daemon instance is inefficient and wasteful. in future
    // versions of the build hook (where we don't need one hook process per
    // build) we should change this to using a daemon connection, ideally a
    // daemon connection provided by the parent via file descriptor passing
    auto store = TRY_AWAIT(openStore(settings.storeUri, {}, AllowDaemon::Disallow));

    auto drvPath = from(context.getParams().getDrvPath(), *store);

    auto machine = rust::lix::machines::Machine::new_(
        rust::to_string(rpc::to<std::string>(context.getParams().getMachine().getName())).to_owned(),
        rust::to_string(rpc::to<std::string>(context.getParams().getMachine().getUri())).to_owned(),
        rust::to_hash_set(rpc::to<StringSet>(context.getParams().getMachine().getSystemTypes())),
        rust::to_string(rpc::to<std::string>(context.getParams().getMachine().getSshKey())).to_owned(),
        context.getParams().getMachine().getMaxJobs(),
        context.getParams().getMachine().getSpeedFactor(),
        rust::to_hash_set(rpc::to<StringSet>(context.getParams().getMachine().getSupportedFeatures())),
        rust::to_hash_set(rpc::to<StringSet>(context.getParams().getMachine().getMandatoryFeatures())),
        rust::to_string(rpc::to<std::string>(context.getParams().getMachine().getSshPublicHostKey()))
            .to_owned(),
        true
    );

    std::shared_ptr<Store> sshStore;

    try {
        auto act = logger->startActivity(
            lvlTalkative, actUnknown, fmt("connecting to '%s'", to_std_string(machine.name.as_str()))
        );
        sshStore = TRY_AWAIT(openStore(machine));
    } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
        RPC_FILL(context.getResults().initResult(), setStoreConnectionFailed, e.what());
        co_return result::success();
    }

    auto builder = BuilderConnection{sshStore, to_std_string(machine.uri.as_str())};

    auto ac = context.getResults().initResult().initAccept();
    ac.setMachine(kj::heap<AcceptedBuild>(store, drvPath, std::move(builder)));

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<void> Instance::build(BuildContext context)
{
    return RPC_IMPL({
        if (used) {
            throw Error("build hooks can only accept a single job");
        }
        used = true; // lock out other rpc calls during processing
        auto result = co_await buildImpl(context);
        used = result.has_value() && context.getResults().getResult().isAccept();
        result.value();
    });
}

kj::Promise<void> AcceptedBuild::run(RunContext context)
{
    return RPC_IMPL({
        if (used) {
            throw Error("build hooks builds are single-use items");
        }
        auto oldLogger = logger;
        logger = rpc::log::makeRpcLoggerClient(context.getParams().getLogger());
        TRY_AWAIT(oldLogger->flush());
        used = true;
        this->oldLogger = oldLogger;
        auto result = co_await runImpl(context);
        result.value();
    });
}

kj::Promise<Result<void>> AcceptedBuild::runImpl(RunContext context)
{
    try {
        auto & sshStore = builder.sshStore;
        auto & storeUri = builder.storeUri;

        auto lockDir = rpc::to<Path>(context.getParams().getLockDir());
        auto inputs = rpc::to<std::set<StorePath>>(context.getParams().getInputs(), *store);
        auto wantedOutputs = rpc::to<std::set<std::string>>(context.getParams().getWantedOutputs());

        auto lockFileName = lockDir + "/" + makeLockFilename(storeUri) + ".upload-lock";

        AutoCloseFD uploadLock = openLockFile(lockFileName, true);

        {
            auto act = logger->startActivity(
                lvlTalkative, actUnknown, fmt("waiting for the upload lock to '%s'", storeUri)
            );

            auto result = TRY_AWAIT(
                AIO().timeoutAfter(15 * kj::MINUTES, lockFileAsync(uploadLock.get(), ltWrite))
            );
            if (!result) {
                printError("somebody is hogging the upload lock for '%s', continuing...", storeUri);
            }
        }

        auto substitute = settings.buildersUseSubstitutes ? Substitute : NoSubstitute;

        {
            auto act = logger->startActivity(
                lvlTalkative, actUnknown, fmt("copying dependencies to '%s'", storeUri)
            );
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

        auto & result = *optResult;
        if (!result.success()) {
            throw Error(
                "build of '%s' on '%s' failed: %s",
                store->printStorePath(drvPath),
                storeUri,
                result.errorMsg
            );
        }

        StorePathSet missingPaths;
        auto outputPaths = drv.outputsAndPaths(*store);
        for (auto & [outputName, outputPath] : outputPaths) {
            if (!TRY_AWAIT(store->isValidPath(outputPath.second))) {
                missingPaths.insert(outputPath.second);
            }
        }

        if (!missingPaths.empty()) {
            auto act = logger->startActivity(
                lvlTalkative, actUnknown, fmt("copying outputs from '%s'", storeUri)
            );
            if (auto localStore = store.try_cast_shared<LocalStore>())
                for (auto & path : missingPaths)
                    localStore->locksHeld.insert(store->printStorePath(path)); /* FIXME: ugly */
            TRY_AWAIT(
                copyPaths(*sshStore, *store, missingPaths, NoRepair, NoCheckSigs, NoSubstitute)
            );
        }

        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }
}

void registerLegacyBuildRemote() {
    LegacyCommandRegistry::add("build-remote", main_build_remote);
}

}
