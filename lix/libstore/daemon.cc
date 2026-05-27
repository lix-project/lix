#include "lix/libstore/daemon.hh"
#include "filetransfer.hh"
#include "libutil/async.hh"
#include "libutil/logging-rpc.hh"
#include "libutil/result.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/monitor-fd.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libstore/worker-protocol-impl.hh"
#include "lix/libstore/build-result.hh" // IWYU pragma: keep
#include "lix/libstore/store-api.hh"
#include "lix/libstore/store-cast.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libstore/log-store.hh"
#include "lix/libstore/indirect-root-store.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/types-rpc.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/args.hh"
#include "lix/libstore/daemon.capnp.h"
#include "lix/libutil/rpc.hh"
#include "lix/libutil/types-rpc.hh"

#include <boost/core/demangle.hpp>
#include <capnp/rpc-twoparty.h>
#include <cstdint>
#include <ctime>
#include <kj/async.h>
#include <kj/encoding.h>
#include <kj/exception.h>
#include <kj/memory.h>
#include <sstream>

const std::string nix::rpc::daemon::UNSTABLE_LEGACY_TUNNELED = "lix/legacy/" PACKAGE_VERSION;

namespace nix::daemon {

Sink & operator << (Sink & sink, const Logger::Fields & fields)
{
    sink << fields.size();
    for (auto & f : fields) {
        sink << f.type;
        if (f.type == Logger::Field::tInt)
            sink << f.i;
        else if (f.type == Logger::Field::tString)
            sink << f.s;
        else abort();
    }
    return sink;
}

/* Logger that forwards log messages to the client, *if* we're in a
   state where the protocol allows it (i.e., when canSendStderr is
   true). */
struct TunnelLogger : public Logger
{
    FdSink & to;

    struct State
    {
        bool canSendStderr = false;
        std::vector<std::string> pendingMsgs;
    };

    Sync<State> state_;

    /**
     * Worker protocol version of the other side. May be newer than this daemon.
     */
    const WorkerProto::Version clientVersion;

    TunnelLogger(FdSink & to, WorkerProto::Version clientVersion)
        : to(to), clientVersion(clientVersion) {
        assert(clientVersion >= MIN_SUPPORTED_WORKER_PROTO_VERSION);
    }

    BufferState enqueueMsg(const std::string & s)
    {
        auto state(state_.lock());

        if (state->canSendStderr) {
            assert(state->pendingMsgs.empty());
            try {
                to(s);
                to.flush();
            } catch (...) {
                /* Write failed; that means that the other side is
                   gone. */
                state->canSendStderr = false;
                throw;
            }
        } else
            state->pendingMsgs.push_back(s);
        return BufferState::HasSpace;
    }

    BufferState log(Verbosity lvl, std::string_view s) override
    {
        if (lvl > getVerbosity()) {
            return BufferState::HasSpace;
        }

        StringSink buf;
        buf << STDERR_NEXT << (s + "\n");
        return enqueueMsg(buf.s);
    }

    BufferState logEI(const ErrorInfo & ei) override
    {
        if (ei.level > getVerbosity()) {
            return BufferState::HasSpace;
        }

        std::stringstream oss;
        showErrorInfo(oss, ei, false);

        StringSink buf;
        buf << STDERR_NEXT << oss.str();
        return enqueueMsg(buf.s);
    }

    /* startWork() means that we're starting an operation for which we
       want to send out stderr to the client. */
    void startWork()
    {
        auto state(state_.lock());
        state->canSendStderr = true;

        for (auto & msg : state->pendingMsgs)
            to(msg);

        state->pendingMsgs.clear();

        to.flush();
    }

    /* stopWork() means that we're done; stop sending stderr to the
       client. */
    void stopWork(const Error * ex = nullptr)
    {
        auto state(state_.lock());

        state->canSendStderr = false;

        if (!ex)
            to << STDERR_LAST;
        else {
            to << STDERR_ERROR << *ex;
        }
    }

    BufferState startActivityImpl(
        ActivityId act,
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        const Fields & fields,
        ActivityId parent
    ) override
    {
        StringSink buf;
        buf << STDERR_START_ACTIVITY << act << lvl << type << s << fields << parent;
        return enqueueMsg(buf.s);
    }

    BufferState stopActivityImpl(ActivityId act) override
    {
        StringSink buf;
        buf << STDERR_STOP_ACTIVITY << act;
        return enqueueMsg(buf.s);
    }

    BufferState resultImpl(ActivityId act, ResultType type, const Fields & fields) override
    {
        StringSink buf;
        buf << STDERR_RESULT << act << type << fields;
        return enqueueMsg(buf.s);
    }
};

struct ClientSettings
{
    bool keepFailed;
    bool keepGoing;
    bool tryFallback;
    Verbosity verbosity;
    unsigned int maxBuildJobs;
    time_t maxSilentTime;
    bool verboseBuild;
    unsigned int buildCores;
    bool useSubstitutes;
    StringMap overrides;

    void apply(TrustedFlag trusted)
    {
        settings.keepFailed.override(keepFailed);
        settings.keepGoing.override(keepGoing);
        settings.tryFallback.override(tryFallback);
        setVerbosity(verbosity);
        settings.maxBuildJobs.override(maxBuildJobs);
        settings.maxSilentTime.override(maxSilentTime);
        settings.verboseBuild = verboseBuild;
        settings.buildCores.override(buildCores);
        settings.useSubstitutes.override(useSubstitutes);

        for (auto & i : overrides) {
            auto & name(i.first);
            auto & value(i.second);

            auto setSubstituters = [&](Setting<Strings> & res) {
                if (name != res.name && res.aliases.count(name) == 0)
                    return false;
                StringSet trusted = settings.trustedSubstituters;
                for (auto & s : settings.substituters.get())
                    trusted.insert(s);
                Strings subs;
                auto ss = tokenizeString<Strings>(value);
                for (auto & s : ss)
                    if (trusted.count(s))
                        subs.push_back(s);
                    else if (!s.ends_with("/") && trusted.count(s + "/"))
                        subs.push_back(s + "/");
                    else
                        printTaggedWarning(
                            "ignoring untrusted substituter '%s', you are not a trusted user.\n"
                            "Run `man nix.conf` for more information on the `substituters` "
                            "configuration option.",
                            s
                        );
                res.override(subs);
                return true;
            };

            try {
                if (name == "ssh-auth-sock" // obsolete
                    || name == "store") // the daemon *is* the store
                    ;
                else if (name == experimentalFeatureSettings.experimentalFeatures.name) {
                    // We don’t want to forward the experimental features to
                    // the daemon, as that could cause some pretty weird stuff
                    if (parseFeatures(tokenizeString<StringSet>(value)) != experimentalFeatureSettings.experimentalFeatures.get())
                        debug("Ignoring the client-specified experimental features");
                } else if (name == settings.pluginFiles.name) {
                    if (tokenizeString<Paths>(value) != settings.pluginFiles.get())
                        printTaggedWarning(
                            "Ignoring the client-specified plugin-files.\n"
                            "The client specifying plugins to the daemon never made sense, and was "
                            "removed in Nix."
                        );
                } else if (trusted || name == settings.buildTimeout.name
                           || name == settings.maxSilentTime.name
                           || name == settings.pollInterval.name
                           || name == fileTransferSettings.maxConnectTimeout.name
                           || fileTransferSettings.initialConnectTimeout.isNameOrAlias(name)
                           || (name == "builders" && value == ""))
                {
                    settings.set(name, value);
                } else if (setSubstituters(settings.substituters))
                    ;
                else
                    printTaggedWarning(
                        "Ignoring the client-specified setting '%s', because it is a restricted "
                        "setting and you are not a trusted user",
                        name
                    );
            } catch (UsageError & e) {
                printTaggedWarning("%1%", Uncolored(e.what()));
            }
        }
    }
};

static void performOp(AsyncIoRoot & aio, TunnelLogger * logger, ref<Store> store,
    TrustedFlag trusted, WorkerProto::Version clientVersion,
    Source & from, BufferedSink & to, WorkerProto::Op op)
{
    WorkerProto::ReadConn rconn{from, *store, clientVersion};
    WorkerProto::WriteConn wconn{*store, clientVersion};

    switch (op) {

    case WorkerProto::Op::IsValidPath: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        bool result = aio.blockOn(store->isValidPath(path));
        logger->stopWork();
        to << result;
        break;
    }

    case WorkerProto::Op::QueryValidPaths: {
        auto paths = WorkerProto::Serialise<StorePathSet>::read(rconn);

        SubstituteFlag substitute = readNum<unsigned>(from) ? Substitute : NoSubstitute;

        logger->startWork();
        if (substitute) {
            aio.blockOn(store->substitutePaths(paths));
        }
        auto res = aio.blockOn(store->queryValidPaths(paths, substitute));
        logger->stopWork();
        to << WorkerProto::write(wconn, res);
        break;
    }

    case WorkerProto::Op::QuerySubstitutablePaths: {
        auto paths = WorkerProto::Serialise<StorePathSet>::read(rconn);
        logger->startWork();
        auto res = aio.blockOn(store->querySubstitutablePaths(paths));
        logger->stopWork();
        to << WorkerProto::write(wconn, res);
        break;
    }

    case WorkerProto::Op::HasSubstitutes: {
        throw UnimplementedError("HasSubstitutes is not supported in Lix. This is not used if the declared server protocol is > 1.12 (Nix 1.0, 2012)");
        break;
    }

    case WorkerProto::Op::QueryPathHash: {
        throw UnimplementedError("QueryPathHash is not supported in Lix, client usages were removed in 2016 in e0204f8d462041387651af388074491fd0bf36d6");
        break;
    }

    case WorkerProto::Op::QueryReferences: {
        throw UnimplementedError("QueryReferences is not supported in Lix, client usages were removed in 2016 in e0204f8d462041387651af388074491fd0bf36d6");
        break;
    }

    case WorkerProto::Op::QueryDeriver: {
        throw UnimplementedError("QueryDeriver is not supported in Lix, client usages were removed in 2016 in e0204f8d462041387651af388074491fd0bf36d6");
        break;
    }

    case WorkerProto::Op::ExportPath: {
        throw UnimplementedError("ExportPath is not supported in Lix, client usage were removed in 2017 in 27dc76c1a5dbe654465245ff5f6bc22e2c8902da");
        break;
    }

    case WorkerProto::Op::ImportPaths: {
        throw UnimplementedError("ImportPaths is not supported in Lix. This is not used if the declared server protocol is >= 1.18 (Nix 2.0, 2016)");
        break;
    }

    case WorkerProto::Op::QueryDerivationOutputs: {
        throw UnimplementedError("QueryDerivationOutputs is not supported in Lix. This is not used if the declared server protocol is >= 1.21 (Nix 2.4)");
    }

    case WorkerProto::Op::QueryReferrers:
    case WorkerProto::Op::QueryValidDerivers: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        StorePathSet paths;

        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wswitch-enum"
        switch (op) {
            case WorkerProto::Op::QueryReferrers: {
                aio.blockOn(store->queryReferrers(path, paths));
                break;
            }
            case WorkerProto::Op::QueryValidDerivers: {
                paths = aio.blockOn(store->queryValidDerivers(path));
                break;
            }
            default:
                abort();
                break;
        }
        #pragma GCC diagnostic pop

        logger->stopWork();
        to << WorkerProto::write(wconn, paths);
        break;
    }

    case WorkerProto::Op::QueryDerivationOutputNames: {
        throw UnimplementedError("QueryDerivationOutputNames is not supported in Lix. This is not used if the declared server protocol is >= 1.31 (Nix 2.4)");
    }

    case WorkerProto::Op::QueryDerivationOutputMap: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        auto outputs = aio.blockOn(store->queryDerivationOutputMap(path));
        logger->stopWork();
        to << WorkerProto::write(wconn, outputs);
        break;
    }

    case WorkerProto::Op::QueryPathFromHashPart: {
        auto hashPart = readString(from);
        logger->startWork();
        auto path = aio.blockOn(store->queryPathFromHashPart(hashPart));
        logger->stopWork();
        to << (path ? store->printStorePath(*path) : "");
        break;
    }

    case WorkerProto::Op::AddToStore: {
        auto name = readString(from);
        auto camStr = readString(from);
        auto refs = WorkerProto::Serialise<StorePathSet>::read(rconn);
        bool repairBool = readBool(from);
        auto repair = RepairFlag{repairBool};

        logger->startWork();
        auto pathInfo = [&]() {
            // NB: FramedSource must be out of scope before logger->stopWork();
            auto [contentAddressMethod, hashType_] = ContentAddressMethod::parse(camStr);
            auto hashType = hashType_; // work around clang bug
            FramedSource source(from);
            // TODO this is essentially RemoteStore::addCAToStore. Move it up to Store.
            return std::visit(overloaded {
                [&](const TextIngestionMethod &) {
                    if (hashType != HashType::SHA256)
                        throw UnimplementedError("When adding text-hashed data called '%s', only SHA-256 is supported but '%s' was given",
                            name, printHashType(hashType));
                    // We could stream this by changing Store
                    std::string contents = source.drain();
                    auto path = aio.blockOn(store->addTextToStore(name, contents, refs, repair));
                    return aio.blockOn(store->queryPathInfo(path));
                },
                [&](const FileIngestionMethod & fim) {
                    AsyncSourceInputStream stream{source};
                    auto path = aio.blockOn(
                        store->addToStoreFromDump(stream, name, fim, hashType, repair, refs)
                    );
                    return aio.blockOn(store->queryPathInfo(path));
                },
            }, contentAddressMethod.raw);
        }();
        logger->stopWork();

        to << WorkerProto::Serialise<ValidPathInfo>::write(wconn, *pathInfo);
        break;
    }

    case WorkerProto::Op::AddMultipleToStore: {
        bool repair = readBool(from);
        bool dontCheckSigs = readBool(from);
        if (!trusted && dontCheckSigs)
            dontCheckSigs = false;

        logger->startWork();
        {
            FramedSource source(from);
            auto expected = readNum<uint64_t>(source);
            for (uint64_t i = 0; i < expected; ++i) {
                auto info = WorkerProto::Serialise<ValidPathInfo>::read(
                    WorkerProto::ReadConn{source, *store, clientVersion}
                );
                info.ultimate = false; // duplicated in RemoteStore::addMultipleToStore
                AsyncSourceInputStream stream{source};
                aio.blockOn(store->addToStore(
                    info, stream, RepairFlag{repair}, dontCheckSigs ? NoCheckSigs : CheckSigs
                ));
            }
        }
        logger->stopWork();
        break;
    }

    case WorkerProto::Op::AddTextToStore: {
        throw UnimplementedError("AddTextToStore is not supported in Lix. This is not used if the declared server protocol is >= 1.25 (Nix 2.4)");
    }

    case WorkerProto::Op::BuildPaths: {
        auto drvs = WorkerProto::Serialise<DerivedPaths>::read(rconn);
        BuildMode mode = buildModeFromInteger(readNum<unsigned>(from));

        /* Repairing is not atomic, so disallowed for "untrusted"
           clients.

           FIXME: layer violation in this message: the daemon code (i.e.
           this file) knows whether a client/connection is trusted, but it
           does not how how the client was authenticated. The mechanism
           need not be getting the UID of the other end of a Unix Domain
           Socket.
          */
        if (mode == bmRepair && !trusted)
            throw Error("repairing is not allowed because you are not in 'trusted-users'");
        logger->startWork();
        aio.blockOn(store->buildPaths(drvs, mode));
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::BuildPathsWithResults: {
        auto drvs = WorkerProto::Serialise<DerivedPaths>::read(rconn);
        BuildMode mode = bmNormal;
        mode = buildModeFromInteger(readNum<unsigned>(from));

        /* Repairing is not atomic, so disallowed for "untrusted"
           clients.

           FIXME: layer violation; see above. */
        if (mode == bmRepair && !trusted)
            throw Error("repairing is not allowed because you are not in 'trusted-users'");

        logger->startWork();
        auto results = aio.blockOn(store->buildPathsWithResults(drvs, mode));
        logger->stopWork();

        to << WorkerProto::write(wconn, results);

        break;
    }

    case WorkerProto::Op::BuildDerivation: {
        auto drvPath = store->parseStorePath(readString(from));
        BasicDerivation drv;
        /*
         * Note: unlike wopEnsurePath, this operation reads a
         * derivation-to-be-realized from the client with
         * readDerivation(Source,Store) rather than reading it from
         * the local store with Store::readDerivation().  Since the
         * derivation-to-be-realized is not registered in the store
         * it cannot be trusted that its outPath was calculated
         * correctly.
         */
        readDerivation(from, *store, drv, Derivation::nameFromPath(drvPath));
        BuildMode buildMode = buildModeFromInteger(readNum<unsigned>(from));
        logger->startWork();

        auto drvType = drv.type();

        /* Content-addressed derivations are trustless because their output paths
           are verified by their content alone, so any derivation is free to
           try to produce such a path.

           Input-addressed derivation output paths, however, are calculated
           from the derivation closure that produced them---even knowing the
           root derivation is not enough. That the output data actually came
           from those derivations is fundamentally unverifiable, but the daemon
           trusts itself on that matter. The question instead is whether the
           submitted plan has rights to the output paths it wants to fill, and
           at least the derivation closure proves that.

           It would have been nice if input-address algorithm merely depended
           on the build time closure, rather than depending on the derivation
           closure. That would mean input-addressed paths used at build time
           would just be trusted and not need their own evidence. This is in
           fact fine as the same guarantees would hold *inductively*: either
           the remote builder has those paths and already trusts them, or it
           needs to build them too and thus their evidence must be provided in
           turn.  The advantage of this variant algorithm is that the evidence
           for input-addressed paths which the remote builder already has
           doesn't need to be sent again.

           That said, now that we have floating CA derivations, it is better
           that people just migrate to those which also solve this problem, and
           others. It's the same migration difficulty with strictly more
           benefit.

           Lastly, do note that when we parse fixed-output content-addressed
           derivations, we throw out the precomputed output paths and just
           store the hashes, so there aren't two competing sources of truth an
           attacker could exploit. */
        if (!(drvType.isCA() || trusted))
            throw Error("you are not privileged to build input-addressed derivations");

        /* Make sure that the non-input-addressed derivations that got this far
           are in fact content-addressed if we don't trust them. */
        assert(drvType.isCA() || trusted);

        /* Recompute the derivation path when we cannot trust the original. */
        if (!trusted) {
            /* Recomputing the derivation path for input-address derivations
               makes it harder to audit them after the fact, since we need the
               original not-necessarily-resolved derivation to verify the drv
               derivation as adequate claim to the input-addressed output
               paths. */
            assert(drvType.isCA());

            Derivation drv2;
            static_cast<BasicDerivation &>(drv2) = drv;
            drvPath = aio.blockOn(writeDerivation(*store, Derivation { drv2 }));
        }

        auto res = aio.blockOn(store->buildDerivation(drvPath, drv, buildMode));
        logger->stopWork();
        to << WorkerProto::write(wconn, res);
        break;
    }

    case WorkerProto::Op::EnsurePath: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        aio.blockOn(store->ensurePath(path));
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::AddTempRoot: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        aio.blockOn(store->addTempRoot(path));
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::AddIndirectRoot: {
        Path path = absPath(readString(from));

        logger->startWork();
        auto & indirectRootStore = require<IndirectRootStore>(*store);
        aio.blockOn(indirectRootStore.addIndirectRoot(path));
        logger->stopWork();

        to << 1;
        break;
    }

    // Obsolete since 9947f1646a26b339fff2e02b77798e9841fac7f0 (included in CppNix 2.5.0).
    case WorkerProto::Op::SyncWithGC: {
        throw UnimplementedError("SyncWithGC is not supported in Lix. This is not used if the declared server protocol is >= 1.31 (Nix 2.5)");
    }

    case WorkerProto::Op::FindRoots: {
        logger->startWork();
        auto & gcStore = require<GcStore>(*store);
        Roots roots = aio.blockOn(gcStore.findRoots(!trusted));
        logger->stopWork();

        size_t size = 0;
        for (auto & i : roots)
            size += i.second.size();

        to << size;

        for (auto & [target, links] : roots)
            for (auto & link : links)
                to << link << store->printStorePath(target);

        break;
    }

    case WorkerProto::Op::CollectGarbage: {
        GCOptions options;
        options.action = (GCOptions::GCAction) readNum<unsigned>(from);
        options.pathsToDelete = WorkerProto::Serialise<StorePathSet>::read(rconn);
        options.ignoreLiveness = readBool(from);
        options.maxFreed = readNum<uint64_t>(from);
        // obsolete fields
        readNum<unsigned>(from);
        readNum<unsigned>(from);
        readNum<unsigned>(from);

        GCResults results;

        logger->startWork();
        if (options.ignoreLiveness)
            throw Error("ignore-liveness is not supported via the Lix daemon; try running the command again with `--store local` and as the user that owns the Nix store (usually root)");
        auto & gcStore = require<GcStore>(*store);
        aio.blockOn(gcStore.collectGarbage(options, results));
        logger->stopWork();

        to << results.paths << results.bytesFreed << 0 /* obsolete */;

        break;
    }

    case WorkerProto::Op::SetOptions: {

        ClientSettings clientSettings;

        clientSettings.keepFailed = readNum<unsigned>(from);
        clientSettings.keepGoing = readNum<unsigned>(from);
        clientSettings.tryFallback = readNum<unsigned>(from);
        clientSettings.verbosity = (Verbosity) readNum<unsigned>(from);
        clientSettings.maxBuildJobs = readNum<unsigned>(from);
        clientSettings.maxSilentTime = readNum<unsigned>(from);
        readNum<unsigned>(from); // obsolete useBuildHook
        clientSettings.verboseBuild = lvlError == (Verbosity) readNum<unsigned>(from);
        readNum<unsigned>(from); // obsolete logType
        readNum<unsigned>(from); // obsolete printBuildTrace
        clientSettings.buildCores = readNum<unsigned>(from);
        clientSettings.useSubstitutes = readNum<unsigned>(from);

        unsigned int n = readNum<unsigned>(from);
        for (unsigned int i = 0; i < n; i++) {
            auto name = readString(from);
            auto value = readString(from);
            clientSettings.overrides.emplace(name, value);
        }

        logger->startWork();
        clientSettings.apply(trusted);
        logger->stopWork();
        break;
    }

    case WorkerProto::Op::QuerySubstitutablePathInfo: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        SubstitutablePathInfos infos;
        aio.blockOn(store->querySubstitutablePathInfos({{path, std::nullopt}}, infos));
        logger->stopWork();
        auto i = infos.find(path);
        if (i == infos.end())
            to << 0;
        else {
            to << 1
               << (i->second.deriver ? store->printStorePath(*i->second.deriver) : "");
            to << WorkerProto::write(wconn, i->second.references);
            to << i->second.downloadSize
               << i->second.narSize;
        }
        break;
    }

    case WorkerProto::Op::QuerySubstitutablePathInfos: {
        SubstitutablePathInfos infos;
        StorePathCAMap pathsMap = WorkerProto::Serialise<StorePathCAMap>::read(rconn);
        logger->startWork();
        aio.blockOn(store->querySubstitutablePathInfos(pathsMap, infos));
        logger->stopWork();
        to << WorkerProto::write(wconn, infos);
        break;
    }

    case WorkerProto::Op::QueryAllValidPaths: {
        logger->startWork();
        auto paths = aio.blockOn(store->queryAllValidPaths());
        logger->stopWork();
        to << WorkerProto::write(wconn, paths);
        break;
    }

    case WorkerProto::Op::QueryPathInfo: {
        auto path = store->parseStorePath(readString(from));
        std::shared_ptr<const ValidPathInfo> info;
        logger->startWork();
        try {
            info = aio.blockOn(store->queryPathInfo(path));
        } catch (InvalidPath &) {
            // The path being invalid isn't fatal here since it will just be
            // sent as not present.
        }
        logger->stopWork();
        if (info) {
            to << 1;
            to << WorkerProto::write(wconn, static_cast<const UnkeyedValidPathInfo &>(*info));
        } else {
            to << 0;
        }
        break;
    }

    case WorkerProto::Op::OptimiseStore:
        logger->startWork();
        aio.blockOn(store->optimiseStore());
        logger->stopWork();
        to << 1;
        break;

    case WorkerProto::Op::VerifyStore: {
        bool checkContents = readBool(from);
        bool repair = readBool(from);
        logger->startWork();
        if (repair && !trusted)
            throw Error("you are not privileged to repair paths");
        bool errors = aio.blockOn(store->verifyStore(checkContents, (RepairFlag) repair));
        logger->stopWork();
        to << errors;
        break;
    }

    case WorkerProto::Op::AddSignatures: {
        auto path = store->parseStorePath(readString(from));
        StringSet sigs = readStrings<StringSet>(from);
        logger->startWork();
        aio.blockOn(store->addSignatures(path, sigs));
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::NarFromPath: {
        auto path = store->parseStorePath(readString(from));
        logger->startWork();
        logger->stopWork();
        to << dumpPath(store->toRealPath(path));
        break;
    }

    case WorkerProto::Op::AddToStoreNar: {
        bool repair, dontCheckSigs;
        auto path = store->parseStorePath(readString(from));
        auto deriver = readString(from);
        auto narHash = Hash::parseAny(readString(from), HashType::SHA256);
        ValidPathInfo info { path, narHash };
        if (deriver != "")
            info.deriver = store->parseStorePath(deriver);
        info.references = WorkerProto::Serialise<StorePathSet>::read(rconn);
        info.registrationTime = readNum<time_t>(from);
        info.narSize = readNum<uint64_t>(from);
        info.ultimate = readBool(from);
        info.sigs = readStrings<StringSet>(from);
        info.ca = ContentAddress::parseOpt(readString(from));
        repair = readBool(from);
        dontCheckSigs = readBool(from);
        if (!trusted && dontCheckSigs)
            dontCheckSigs = false;
        if (!trusted)
            info.ultimate = false;

        logger->startWork();
        {
            FramedSource source(from);
            AsyncSourceInputStream stream{source};
            aio.blockOn(store->addToStore(info, stream, (RepairFlag) repair,
                dontCheckSigs ? NoCheckSigs : CheckSigs));
        }
        logger->stopWork();

        break;
    }

    case WorkerProto::Op::QueryMissing: {
        auto targets = WorkerProto::Serialise<DerivedPaths>::read(rconn);
        logger->startWork();
        StorePathSet willBuild, willSubstitute, unknown;
        uint64_t downloadSize, narSize;
        aio.blockOn(
            store->queryMissing(targets, willBuild, willSubstitute, unknown, downloadSize, narSize)
        );
        logger->stopWork();
        to << WorkerProto::write(wconn, willBuild);
        to << WorkerProto::write(wconn, willSubstitute);
        to << WorkerProto::write(wconn, unknown);
        to << downloadSize << narSize;
        break;
    }

    case WorkerProto::Op::RegisterDrvOutput:
    case WorkerProto::Op::QueryRealisation: {
        throw UnimplementedError("ca derivations are not supported");
    }

    case WorkerProto::Op::AddBuildLog: {
        StorePath path{readString(from)};
        logger->startWork();
        auto & logStore = require<LogStore>(*store);
        {
            FramedSource source(from);
            if (!trusted) {
                throw Error("you are not privileged to add logs");
            }
            StringSink sink;
            source.drainInto(sink);
            aio.blockOn(logStore.addBuildLog(path, sink.s));
        }
        logger->stopWork();
        to << 1;
        break;
    }

    case WorkerProto::Op::QueryFailedPaths:
    case WorkerProto::Op::ClearFailedPaths:
        throw Error("Removed operation %1%", op);

    default:
        throw Error("invalid operation %1%", op);
    }
}

static void processLegacyRequests(
    AsyncIoRoot & aio,
    Logger * prevLogger,
    TunnelLogger * tunnelLogger,
    ref<Store> store,
    FdSource & from,
    FdSink & to,
    TrustedFlag trusted,
    WorkerProto::Version clientVersion
)
{
    unsigned int opCount = 0;

    Finally finally([&]() { printMsgUsing(prevLogger, lvlDebug, "%d operations", opCount); });

    while (true) {
        WorkerProto::Op op;
        try {
            op = (enum WorkerProto::Op) readNum<unsigned>(from);
        } catch (Interrupted & e) {
            break;
        } catch (EndOfFile & e) {
            break;
        }

        printMsgUsing(prevLogger, lvlDebug, "received daemon op %d", op);

        opCount++;

        debug("performing daemon worker op: %d", op);

        try {
            KJ_DEFER(aio.blockOn(logger->flush()));
            performOp(aio, tunnelLogger, store, trusted, clientVersion, from, to, op);
        } catch (Error & e) {
            /* If we're not in a state where we can send replies, then
               something went wrong processing the input of the
               client.  This can happen especially if I/O errors occur
               during addTextToStore() / importPath().  If that
               happens, just send the error message and exit. */
            bool errorAllowed = tunnelLogger->state_.lock()->canSendStderr;
            tunnelLogger->stopWork(&e);
            if (!errorAllowed) {
                throw;
            }
        } catch (std::bad_alloc & e) {
            auto ex = Error("Lix daemon out of memory");
            tunnelLogger->stopWork(&ex);
            throw;
        }

        to.flush();

        assert(!tunnelLogger->state_.lock()->canSendStderr);
    }
}

void processLegacyConnection(
    AsyncIoRoot & aio, ref<Store> store, FdSource & from, FdSink & to, TrustedFlag trusted
)
{
    auto monitor = std::make_unique<MonitorFdHup>(from.fd);

    /* Exchange the greeting. */
    unsigned int magic = readNum<unsigned>(from);
    if (magic != WORKER_MAGIC_1) throw Error("protocol mismatch");
    to << WORKER_MAGIC_2 << PROTOCOL_VERSION;
    to.flush();
    WorkerProto::Version clientVersion = readNum<unsigned>(from);

    if (clientVersion < MIN_SUPPORTED_WORKER_PROTO_VERSION)
        throw Error("the Nix client version is too old");

    auto tunnelLogger = new TunnelLogger(to, clientVersion);
    auto prevLogger = nix::logger;
    logger = tunnelLogger;

    // FIXME: what is *supposed* to be in this even?
    if (readNum<unsigned>(from)) {
        // Obsolete CPU affinity.
        readNum<unsigned>(from);
    }

    readNum<unsigned>(from); // obsolete reserveSpace

    to << nixVersion;

    // We and the underlying store both need to trust the client for
    // it to be trusted.
    auto temp = trusted
        ? aio.blockOn(store->isTrustedClient())
        : std::optional { NotTrusted };
    WorkerProto::WriteConn wconn {*store, clientVersion};
    to << WorkerProto::write(wconn, temp);

    /* Send startup error messages to the client. */
    tunnelLogger->startWork();

    try {
        tunnelLogger->stopWork();
        to.flush();

        processLegacyRequests(aio, prevLogger, tunnelLogger, store, from, to, trusted, clientVersion);
    } catch (Error & e) {
        tunnelLogger->stopWork(&e);
        to.flush();
        return;
    } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
        auto ex = Error(
            "Unexpected exception on the Lix daemon; this is a bug in Lix.\nWe would appreciate a report of the circumstances it happened in at https://git.lix.systems/lix-project/lix.\n%s: %s",
            Uncolored(boost::core::demangle(typeid(e).name())),
            e.what()
        );
        tunnelLogger->stopWork(&ex);
        to.flush();
        // Crash for good measure, so something winds up in system logs and a core dump is generated as well.
        std::terminate();
        return;
    }
}

namespace {
using namespace rpc::daemon;

// Shared state for all legacy protocol implementation structs
struct LegacyState
{
    ref<Store> store;
    TrustedFlag trusted;

    LegacyState(ref<Store> store, TrustedFlag trusted) : store(store), trusted(trusted) {}
};

struct RequestStreamImpl final : LegacyStream::Server
{
    ref<LegacyState> state;
    std::exception_ptr error;
    AsyncFdIoStream workerSock;
    kj::Promise<void> responseForwarder;

    RequestStreamImpl(
        ref<LegacyState> state,
        kj::Promise<std::exception_ptr> error,
        LegacyStream::Client callbacks,
        AutoCloseFD workerFd
    )
        : state(state)
        , workerSock(std::move(workerFd))
        , responseForwarder(forwardResponse(callbacks).exclusiveJoin(
              error.then([&](std::exception_ptr e) -> kj::Promise<void> {
                  onError(e);
                  return kj::NEVER_DONE;
              })
          ))
    {
    }

    void onError(std::exception_ptr e)
    {
        if (!error) {
            error = e;
        }
    }

    kj::Promise<void> forwardResponse(LegacyStream::Client callbacks)
    try {
        std::array<char, 8192> buf;
        while (true) {
            if (auto got = TRY_AWAIT(workerSock.read(buf.data(), buf.size())); !got) {
                break;
            } else {
                auto req = callbacks.feedRequest();
                req.initRaw(*got);
                std::copy(buf.begin(), buf.begin() + *got, req.getRaw().begin());
                TRY_AWAIT_RPC(req.send());
            }
        }
        TRY_AWAIT_RPC(callbacks.syncRequest().send());
    } catch (...) {
        onError(std::current_exception());
    }

    kj::Promise<void> feed(FeedContext context) override
    {
        return RPC_IMPL({
            if (error) {
                std::rethrow_exception(error);
            }
            auto bytes = context.getParams().getRaw();
            TRY_AWAIT(workerSock.writeFull(bytes.begin(), bytes.size()));
        });
    }

    kj::Promise<void> sync(SyncContext context) override
    {
        return kj::READY_NOW;
    }
};

struct LegacyProtocolImpl final : LegacyProtocol::Server
{
    ref<LegacyState> state;

    LegacyProtocolImpl(ref<LegacyState> state) : state(state) {}

    kj::Promise<void> ensurePath(EnsurePathContext context) override
    {
        return RPC_IMPL({
            StorePath path = rpc::from(context.getParams().getPath(), *state->store);
            TRY_AWAIT(state->store->ensurePath(path));
        });
    }

    kj::Promise<void> isValidPath(IsValidPathContext context) override
    {
        return RPC_IMPL({
            StorePath path = rpc::from(context.getParams().getPath(), *state->store);
            auto result = TRY_AWAIT(state->store->isValidPath(path));
            context.initResults().setResult(result);
        });
    }

    kj::Promise<void> optimiseStore(OptimiseStoreContext context) override
    {
        return RPC_IMPL({ TRY_AWAIT(state->store->optimiseStore()); });
    }

    kj::Promise<void> queryValidPaths(QueryValidPathsContext context) override
    {
        return RPC_IMPL({
            auto args = context.getParams();

            StorePathSet paths = rpc::to<StorePathSet>(args.getPaths(), *state->store);
            SubstituteFlag substitute = args.getSubstitute() ? Substitute : NoSubstitute;

            if (substitute) {
                TRY_AWAIT(state->store->substitutePaths(paths));
            }
            auto validPaths = TRY_AWAIT(state->store->queryValidPaths(paths, substitute));
            RPC_FILL_LIST(context.initResults(), initResult, validPaths, *state->store);
        });
    }

    kj::Promise<void> querySubstitutablePaths(QuerySubstitutablePathsContext context) override
    {
        return RPC_IMPL({
            StorePathSet paths = rpc::to<StorePathSet>(context.getParams().getPaths(), *state->store);

            auto validPaths = TRY_AWAIT(state->store->querySubstitutablePaths(paths));
            RPC_FILL_LIST(context.initResults(), initResult, validPaths, *state->store);
        });
    }

    kj::Promise<void> queryReferrers(QueryReferrersContext context) override
    {
        return RPC_IMPL({
            StorePath path = rpc::from(context.getParams().getPath(), *state->store);
            StorePathSet paths;

            TRY_AWAIT(state->store->queryReferrers(path, paths));
            RPC_FILL_LIST(context.initResults(), initResult, paths, *state->store);
        });
    }

    kj::Promise<void> queryValidDerivers(QueryValidDeriversContext context) override
    {
        return RPC_IMPL({
            StorePath path = rpc::from(context.getParams().getPath(), *state->store);
            StorePathSet paths = TRY_AWAIT(state->store->queryValidDerivers(path));
            RPC_FILL_LIST(context.initResults(), initResult, paths, *state->store);
        });
    }

    kj::Promise<void> queryDerivationOutputMap(QueryDerivationOutputMapContext context) override
    {
        return RPC_IMPL({
            StorePath path = rpc::from(context.getParams().getPath(), *state->store);
            auto outputs = TRY_AWAIT(state->store->queryDerivationOutputMap(path));
            RPC_FILL_STRUCT(context.initResults(), initResult, outputs, *state->store);
        });
    }
};

struct LegacyBootImpl final : LegacyBoot::Server
{
    ref<LegacyState> state;
    bool used = false;

    LegacyBootImpl(TrustedFlag trusted, ref<Store> store) : state(make_ref<LegacyState>(store, trusted)) {}

    kj::Promise<void> init(InitContext context) override
    try {
        if (used) {
            throw Error("connection already initialized");
        }

        auto prevLogger = logger;
        logger = rpc::log::makeRpcLoggerClient(context.getParams().getLogger());

        auto args = context.getParams();
        auto result = context.initResults().initResult();
        // We and the underlying store both need to trust the client for it to be trusted.
        if (!state->trusted) {
            result.setTrust(LegacyBoot::Trust::UNTRUSTED);
        } else if (auto trust = TRY_AWAIT(state->store->isTrustedClient()); trust) {
            result.setTrust(*trust ? LegacyBoot::Trust::TRUSTED : LegacyBoot::Trust::UNTRUSTED);
        } else {
            result.setTrust(LegacyBoot::Trust::UNKNOWN);
        }
        result.setVersion(PACKAGE_VERSION);

        auto [rpcSock, workerSock] = SocketPair::stream();
        auto pfp = kj::newPromiseAndCrossThreadFulfiller<std::exception_ptr>();

        struct Request
        {
            AutoCloseFD fd;
            kj::Own<kj::CrossThreadPromiseFulfiller<std::exception_ptr>> signal;

            Request(AutoCloseFD fd, kj::Own<kj::CrossThreadPromiseFulfiller<std::exception_ptr>> fulfiller)
                : fd(std::move(fd))
                , signal(std::move(fulfiller))
            {
            }
        };

        auto req = make_ref<Request>(std::move(rpcSock), std::move(pfp.fulfiller));

        auto legacyThread = std::async(std::launch::async, [prevLogger, state{state}, req] {
            AsyncIoRoot aio;
            FdSource from(req->fd.get());
            FdSink to(req->fd.get());
            TunnelLogger logger(to, PROTOCOL_VERSION);

            try {
                processLegacyRequests(
                    aio, prevLogger, &logger, state->store, from, to, state->trusted, PROTOCOL_VERSION
                );
            } catch (Error & e) {
                req->signal->fulfill(std::current_exception());
            } catch (std::bad_alloc & e) {
                req->signal->fulfill(std::make_exception_ptr(Error("Lix daemon out of memory")));
            } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
                // TODO print stack trace to daemon log, maybe crash?
                // boost stacktrace has from_current_exception (at a cost) with not-great symbolization,
                // cpptrace has a *much* better symbolizer (at unknown cost)
                req->signal->fulfill(
                    std::make_exception_ptr(Error(
                        "Unexpected exception on the Lix daemon; this is a bug in Lix.\n"
                        "We would appreciate a report of the circumstances it happened in at "
                        "https://git.lix.systems/lix-project/lix.\n%s: %s",
                        Uncolored(boost::core::demangle(typeid(e).name())),
                        e.what()
                    ))
                );
            } catch (...) {
                // TODO print stack trace to daemon log, maybe crash?
                req->signal->fulfill(
                    std::make_exception_ptr(Error(
                        "Unexpected exception on the Lix daemon; this is a bug in Lix.\n"
                        "We would appreciate a report of the circumstances it happened in at "
                        "https://git.lix.systems/lix-project/lix.\n"
                    ))
                );
            }
        });

        result.setRequestStream(
            kj::heap<RequestStreamImpl>(
                state, std::move(pfp.promise), args.getReplyStream(), std::move(workerSock)
            )
                .attach(std::move(legacyThread))
        );
        result.setProtocol(kj::heap<LegacyProtocolImpl>(state));
        used = true;
    } catch (...) {
        rpc::rethrow_as_rpc_error();
    }
};

struct BootstrapImpl final : Bootstrap::Server
{
    struct ProtocolEntry
    {
        std::string description;
        std::function<rpc::daemon::Protocol::Client(TrustedFlag, ref<Store>)> factory;
    };

    TrustedFlag trusted;
    ref<Store> store;
    std::map<kj::StringPtr, ProtocolEntry> protocols;
    bool used = false;

    BootstrapImpl(TrustedFlag trusted, ref<Store> store) : trusted(trusted), store(store)
    {
        if (store->isThreadSafe()) {
            protocols.emplace(
                rpc::daemon::UNSTABLE_LEGACY_TUNNELED,
                ProtocolEntry{"tunneled legacy wire protocol", [](TrustedFlag trusted, ref<Store> store) {
                                  return kj::heap<LegacyBootImpl>(trusted, store);
                              }}
            );
        }
    }

    kj::Promise<void> supported(SupportedContext context) override
    {
        if (!experimentalFeatureSettings.isEnabled(Xp::RpcSockets)) {
            kj::throwFatalException(
                kj::Exception(
                    kj::Exception::Type::UNIMPLEMENTED, "main", 0, kj::str("rpc sockets not enabled")
                )
            );
        }

        auto result = context.initResults();
        auto protocols = result.initProtocols(this->protocols.size());
        for (auto [i, proto] : enumerate(this->protocols)) {
            protocols[i].setId(proto.first);
            protocols[i].setDescription(proto.second.description);
        }
        return kj::READY_NOW;
    }

    kj::Promise<void> request(RequestContext context) override
    {
        auto id = rpc::to<std::string>(context.getParams().getProtocol());
        if (used) {
            kj::throwFatalException(
                kj::Exception(
                    kj::Exception::Type::FAILED, "main", 0, kj::str("connection already initialized")
                )
            );
        } else if (const auto & protocol = get(protocols, id)) {
            used = true;
            context.initResults().setResult(protocol->factory(trusted, store));
        } else {
            kj::throwFatalException(
                kj::Exception(
                    kj::Exception::Type::UNIMPLEMENTED, "main", 0, kj::str("unsupported protocol", id)
                )
            );
        }
        return kj::READY_NOW;
    }
};
}

kj::Promise<Result<void>>
processConnection(ref<Store> store, kj::AsyncIoStream & connection, TrustedFlag trusted)
try {
    // TODO trace encoders can do neat error info things, use them. we could stuff some serialized
    // error struct into the remote trace field instead of using result types and get pipelineable
    // calls out of it. needs more investigation to say if it's worth the possible reporting skew.
    capnp::TwoPartyServer server{kj::heap<BootstrapImpl>(trusted, store)};

    // NOTE we can't easily disconnect a peer without shutting shutting down the socket connection
    // independently of capnp since capnp does not offer such functionality. shutting down sockets
    // in this manner is very disruptive and pretty unreliable, so we will have to find some other
    // way to disconnect clients. or we just don't do it because the DoS risk is not large anyway.
    //
    // since we have control over the promise we can have the following await finish early to stop
    // processing events, and if we close the socket after that we've dropped the connection. this
    // does not guarantee that responses have been sent though, so we can only do this on requests
    // received *after* a fatal error response has been *sent*, inflicting per-operation overhead.
    {
        auto prevLogger = logger;
        co_await server.accept(connection).exclusiveJoin(connection.whenWriteDisconnected());
        // NOTE: we do not flush the logger here because the connection is already closed! we only
        // delete the non-local logger (if one was set) to ensure all rpc references were dropped.
        if (prevLogger != logger) {
            std::swap(prevLogger, logger);
            delete prevLogger;
        }
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}
}
