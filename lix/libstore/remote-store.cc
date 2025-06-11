#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libstore/remote-fs-accessor.hh"
#include "lix/libstore/build-result.hh"
#include "lix/libstore/remote-store.hh"
#include "lix/libstore/remote-store-connection.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libstore/worker-protocol-impl.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libutil/pool.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/logging.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/thread-name.hh"
#include "lix/libutil/thread-pool.hh"
#include "lix/libutil/types.hh"
#include "path-info.hh"

#include <kj/async.h>
#include <optional>
#include <utility>

namespace nix {

/* TODO: Separate these store impls into different files, give them better names */
RemoteStore::RemoteStore(const RemoteStoreConfig & config)
    : Store(config)
    , connections(make_ref<Pool<Connection>>(
          std::max(1, (int) config.maxConnections),
          [this]() { return openAndInitConnection(); },
          [this](const ref<Connection> & r) {
              return r->to.good() && r->from.good()
                  && std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::steady_clock::now() - r->startTime
                     )
                         .count()
                  < this->config().maxConnectionAge;
          }
      ))
{
}


ref<RemoteStore::Connection> RemoteStore::openConnectionWrapper()
{
    if (failed)
        throw Error("opening a connection to remote store '%s' previously failed", getUri());
    try {
        return openConnection();
    } catch (...) {
        failed = true;
        throw;
    }
}

kj::Promise<Result<ref<RemoteStore::Connection>>> RemoteStore::openAndInitConnection()
try {
    auto conn = openConnection();
    try {
        initConnection(*conn);
        co_return conn;
    } catch (...) {
        failed = true;
        throw;
    }
} catch (...) {
    co_return result::current_exception();
}

void RemoteStore::initConnection(Connection & conn)
{
    /* Send the magic greeting, check for the reply. */
    try {
        conn.store = this;
        conn.from.specialEndOfFileError = "Nix daemon disconnected unexpectedly (maybe it crashed?)";
        conn.to << WORKER_MAGIC_1;
        conn.to.flush();

        uint64_t magic = readLongLong(conn.from);
        if (magic != WORKER_MAGIC_2)
            throw Error("protocol mismatch");

        conn.from >> conn.daemonVersion;
        if (GET_PROTOCOL_MAJOR(conn.daemonVersion) != GET_PROTOCOL_MAJOR(PROTOCOL_VERSION))
            throw Error("Nix daemon protocol version not supported");
        if (GET_PROTOCOL_MINOR(conn.daemonVersion) < MIN_SUPPORTED_MINOR_WORKER_PROTO_VERSION)
            throw Error("the Nix daemon version is too old");
        conn.to << PROTOCOL_VERSION;

        // Obsolete CPU affinity.
        conn.to << 0;

        conn.to << false; // obsolete reserveSpace

        conn.to.flush();
        conn.daemonNixVersion = readString(conn.from);
        conn.remoteTrustsUs = WorkerProto::Serialise<std::optional<TrustedFlag>>::read(conn);

        auto ex = conn.processStderr();
        if (ex) std::rethrow_exception(ex);
    }
    catch (Error & e) {
        throw Error("cannot open connection to remote store '%s': %s", getUri(), e.what());
    }

    setOptions(conn);
}


void RemoteStore::setOptions(Connection & conn)
{
    StringSink command;

    command << WorkerProto::Op::SetOptions
       << settings.keepFailed
       << settings.keepGoing
       << settings.tryFallback
       << verbosity
       << settings.maxBuildJobs
       << settings.maxSilentTime
       << true
       << (settings.verboseBuild ? lvlError : lvlVomit)
       << 0 // obsolete log type
       << 0 /* obsolete print build trace */
       << settings.buildCores
       << settings.useSubstitutes;

    std::map<std::string, Config::SettingInfo> overrides;
    settings.getSettings(overrides, true); // libstore settings
    fileTransferSettings.getSettings(overrides, true);
    overrides.erase(settings.keepFailed.name);
    overrides.erase(settings.keepGoing.name);
    overrides.erase(settings.tryFallback.name);
    overrides.erase(settings.maxBuildJobs.name);
    overrides.erase(settings.maxSilentTime.name);
    overrides.erase(settings.buildCores.name);
    overrides.erase(settings.useSubstitutes.name);
    overrides.erase(loggerSettings.showTrace.name);
    overrides.erase(experimentalFeatureSettings.experimentalFeatures.name);
    overrides.erase(settings.pluginFiles.name);
    overrides.erase(settings.storeUri.name); // the daemon *is* the store
    overrides.erase(settings.tarballTtl.name); // eval-time only, implictly set by flake cli
    command << overrides.size();
    for (auto & i : overrides)
        command << i.first << i.second.value;

    StringSource{command.s}.drainInto(conn.to);
    auto ex = conn.processStderr();
    if (ex) std::rethrow_exception(ex);
}


RemoteStore::ConnectionHandle::~ConnectionHandle()
{
    if (!daemonException && std::uncaught_exceptions()) {
        handle.markBad();
        debug("closing daemon connection because of an exception");
    }
}

void RemoteStore::ConnectionHandle::processStderr(bool flush)
{
    auto ex = handle->processStderr(flush);
    if (ex) {
        daemonException = true;
        std::rethrow_exception(ex);
    }
}


kj::Promise<Result<RemoteStore::ConnectionHandle>> RemoteStore::getConnection()
try {
    co_return ConnectionHandle(TRY_AWAIT(connections->get()), handlerThreads);
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> RemoteStore::setOptions()
try {
    setOptions(*(TRY_AWAIT(getConnection()).handle));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<bool>> RemoteStore::isValidPathUncached(const StorePath & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    co_return TRY_AWAIT(
        conn.sendCommand<unsigned>(WorkerProto::Op::IsValidPath, printStorePath(path))
    );
} catch (...) {
    co_return result::current_exception();
}


kj ::Promise<Result<StorePathSet>>
RemoteStore::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
try {
    auto conn(TRY_AWAIT(getConnection()));
    co_return TRY_AWAIT(conn.sendCommand<StorePathSet>(
        WorkerProto::Op::QueryValidPaths, WorkerProto::write(*conn, paths), maybeSubstitute
    ));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePathSet>> RemoteStore::queryAllValidPaths()
try {
    auto conn(TRY_AWAIT(getConnection()));
    co_return TRY_AWAIT(conn.sendCommand<StorePathSet>(WorkerProto::Op::QueryAllValidPaths));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePathSet>> RemoteStore::querySubstitutablePaths(const StorePathSet & paths)
try {
    auto conn(TRY_AWAIT(getConnection()));
    co_return TRY_AWAIT(conn.sendCommand<StorePathSet>(
        WorkerProto::Op::QuerySubstitutablePaths, WorkerProto::write(*conn, paths)
    ));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::querySubstitutablePathInfos(const StorePathCAMap & pathsMap, SubstitutablePathInfos & infos)
try {
    if (pathsMap.empty()) co_return result::success();

    auto conn(TRY_AWAIT(getConnection()));

    infos = TRY_AWAIT(conn.sendCommand<SubstitutablePathInfos>(
        WorkerProto::Op::QuerySubstitutablePathInfos, WorkerProto::write(*conn, pathsMap)
    ));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<std::shared_ptr<const ValidPathInfo>>>
RemoteStore::queryPathInfoUncached(const StorePath & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    try {
        auto valid =
            TRY_AWAIT(conn.sendCommand<bool>(WorkerProto::Op::QueryPathInfo, printStorePath(path)));
        if (!valid) co_return result::success(nullptr);
    } catch (Error & e) {
        // Ugly backwards compatibility hack. TODO(fj#325): remove.
        if (e.msg().find("is not valid") != std::string::npos)
            co_return result::success(nullptr);
        throw;
    }

    co_return std::make_shared<ValidPathInfo>(
        StorePath{path},
        WorkerProto::Serialise<UnkeyedValidPathInfo>::read(*conn));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::queryReferrers(const StorePath & path,
    StorePathSet & referrers)
try {
    auto conn(TRY_AWAIT(getConnection()));
    TRY_AWAIT(conn.sendCommand(WorkerProto::Op::QueryReferrers, printStorePath(path)));
    for (auto & i : WorkerProto::Serialise<StorePathSet>::read(*conn))
        referrers.insert(i);
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePathSet>> RemoteStore::queryValidDerivers(const StorePath & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    co_return TRY_AWAIT(
        conn.sendCommand<StorePathSet>(WorkerProto::Op::QueryValidDerivers, printStorePath(path))
    );
} catch (...) {
    co_return result::current_exception();
}


kj ::Promise<Result<std::map<std::string, StorePath>>>
RemoteStore::queryDerivationOutputMap(const StorePath & path, Store * evalStore_)
try {
    if (!evalStore_) {
        auto conn(TRY_AWAIT(getConnection()));
        auto tmp = TRY_AWAIT(conn.sendCommand<std::map<std::string, std::optional<StorePath>>>(
            WorkerProto::Op::QueryDerivationOutputMap, printStorePath(path)
        ));
        std::map<std::string, StorePath> result;
        for (auto & [name, outPath] : tmp) {
            if (!outPath) {
                throw Error(
                    "remote responded with unknown outpath for %s^%s", path.to_string(), name
                );
            }
            result.emplace(std::move(name), std::move(*outPath));
        }
        co_return result;
    } else {
        auto & evalStore = *evalStore_;
        auto outputs = TRY_AWAIT(evalStore.queryStaticDerivationOutputMap(path));
        // union with the first branch overriding the statically-known ones
        // when non-`std::nullopt`.
        for (auto && [outputName, optPath] :
                TRY_AWAIT(queryDerivationOutputMap(path, nullptr)))
        {
            outputs.insert_or_assign(std::move(outputName), std::move(optPath));
        }
        co_return outputs;
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<StorePath>>>
RemoteStore::queryPathFromHashPart(const std::string & hashPart)
try {
    auto conn(TRY_AWAIT(getConnection()));
    Path path =
        TRY_AWAIT(conn.sendCommand<std::string>(WorkerProto::Op::QueryPathFromHashPart, hashPart));
    if (path.empty()) co_return std::nullopt;
    co_return parseStorePath(path);
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<ref<const ValidPathInfo>>> RemoteStore::addCAToStore(
    AsyncInputStream & dump,
    std::string_view name,
    ContentAddressMethod caMethod,
    HashType hashType,
    const StorePathSet & references,
    RepairFlag repair)
try {
    auto conn(TRY_AWAIT(getConnection()));

    conn->to
        << WorkerProto::Op::AddToStore
        << name
        << caMethod.render(hashType);
    conn->to << WorkerProto::write(*conn, references);
    conn->to << repair;

    // The dump source may invoke the store, so we need to make some room.
    connections->incCapacity();
    {
        Finally cleanup([&]() { connections->decCapacity(); });
        TRY_AWAIT(conn.withFramedSinkAsync([&](Sink & sink) {
            return dump.drainInto(sink);
        }));
    }

    co_return make_ref<ValidPathInfo>(
        WorkerProto::Serialise<ValidPathInfo>::read(*conn));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePath>> RemoteStore::addToStoreFromDump(
    AsyncInputStream & dump,
    std::string_view name,
    FileIngestionMethod method,
    HashType hashType,
    RepairFlag repair,
    const StorePathSet & references
)
try {
    co_return TRY_AWAIT(addCAToStore(dump, name, method, hashType, references, repair))->path;
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::addToStore(
    const ValidPathInfo & info,
    AsyncInputStream & source,
    RepairFlag repair,
    CheckSigsFlag checkSigs
)
try {
    auto conn(TRY_AWAIT(getConnection()));

    conn->to << WorkerProto::Op::AddToStoreNar
             << printStorePath(info.path)
             << (info.deriver ? printStorePath(*info.deriver) : "")
             << info.narHash.to_string(Base::Base16, false);
    conn->to << WorkerProto::write(*conn, info.references);
    conn->to << info.registrationTime << info.narSize
             << info.ultimate << info.sigs << renderContentAddress(info.ca)
             << repair << !checkSigs;

    auto copier = copyNAR(source);
    TRY_AWAIT(conn.withFramedSinkAsync([&](Sink & sink) {
        return copier->drainInto(sink);
    }));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::addMultipleToStore(
    PathsSource & pathsToCopy,
    Activity & act,
    RepairFlag repair,
    CheckSigsFlag checkSigs)
try {
    auto remoteVersion = TRY_AWAIT(getProtocol());

    auto conn(TRY_AWAIT(getConnection()));
    conn->to
        << WorkerProto::Op::AddMultipleToStore
        << repair
        << !checkSigs;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    TRY_AWAIT(conn.withFramedSinkAsync([&](Sink & sink) -> kj::Promise<Result<void>> {
        try {
            sink << pathsToCopy.size();
            for (auto & [pathInfo, pathSource] : pathsToCopy) {
                sink << WorkerProto::Serialise<ValidPathInfo>::write(
                    WorkerProto::WriteConn {*this, remoteVersion},
                    pathInfo);
                TRY_AWAIT(TRY_AWAIT(pathSource())->drainInto(sink));
            }
            co_return result::success();
        } catch (...) {
            co_return result::current_exception();
        }
    }));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePath>> RemoteStore::addTextToStore(
    std::string_view name,
    std::string_view s,
    const StorePathSet & references,
    RepairFlag repair)
try {
    AsyncStringInputStream source(s);
    co_return TRY_AWAIT(addCAToStore(source, name, TextIngestionMethod {}, HashType::SHA256, references, repair))->path;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> RemoteStore::copyDrvsFromEvalStore(
    const std::vector<DerivedPath> & paths,
    std::shared_ptr<Store> evalStore)
try {
    if (evalStore && evalStore.get() != this) {
        /* The remote doesn't have a way to access evalStore, so copy
           the .drvs. */
        RealisedPath::Set drvPaths2;
        for (const auto & i : paths) {
            std::visit(overloaded {
                [&](const DerivedPath::Opaque & bp) {
                    // Do nothing, path is hopefully there already
                },
                [&](const DerivedPath::Built & bp) {
                    drvPaths2.insert(bp.drvPath.path);
                },
            }, i.raw());
        }
        TRY_AWAIT(copyClosure(*evalStore, *this, drvPaths2));
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj ::Promise<Result<void>> RemoteStore::buildPaths(
    const std::vector<DerivedPath> & drvPaths, BuildMode buildMode, std::shared_ptr<Store> evalStore
)
try {
    TRY_AWAIT(copyDrvsFromEvalStore(drvPaths, evalStore));

    auto conn(TRY_AWAIT(getConnection()));
    TRY_AWAIT(conn.sendCommand<unsigned>(
        WorkerProto::Op::BuildPaths, WorkerProto::write(*conn, drvPaths), buildMode
    ));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::vector<KeyedBuildResult>>> RemoteStore::buildPathsWithResults(
    const std::vector<DerivedPath> & paths,
    BuildMode buildMode,
    std::shared_ptr<Store> evalStore)
try {
    TRY_AWAIT(copyDrvsFromEvalStore(paths, evalStore));

    auto conn(TRY_AWAIT(getConnection()));

    co_return TRY_AWAIT(conn.sendCommand<std::vector<KeyedBuildResult>>(
        WorkerProto::Op::BuildPathsWithResults, WorkerProto::write(*conn, paths), buildMode
    ));
} catch (...) {
    co_return result::current_exception();
}

kj ::Promise<Result<BuildResult>> RemoteStore::buildDerivation(
    const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode
)
try {
    auto conn(TRY_AWAIT(getConnection()));
    co_return TRY_AWAIT(conn.sendCommand<BuildResult>(
        WorkerProto::Op::BuildDerivation,
        printStorePath(drvPath),
        serializeDerivation(*this, drv),
        buildMode
    ));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::ensurePath(const StorePath & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    TRY_AWAIT(conn.sendCommand<unsigned>(WorkerProto::Op::EnsurePath, printStorePath(path)));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::addTempRoot(const StorePath & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    TRY_AWAIT(conn.sendCommand<unsigned>(WorkerProto::Op::AddTempRoot, printStorePath(path)));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Roots>> RemoteStore::findRoots(bool censor)
try {
    auto conn(TRY_AWAIT(getConnection()));

    auto roots = TRY_AWAIT(conn.sendCommand<std::vector<std::tuple<std::string, StorePath>>>(
        WorkerProto::Op::FindRoots
    ));
    Roots result;
    for (auto & [link, target] : roots) {
        result[std::move(target)].emplace(link);
    }
    co_return result;
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>>
RemoteStore::collectGarbage(const GCOptions & options, GCResults & results)
try {
    auto conn(TRY_AWAIT(getConnection()));

    using ResultT = std::tuple<PathSet, uint64_t, uint64_t>;

    std::tie(results.paths, results.bytesFreed, std::ignore) = TRY_AWAIT(conn.sendCommand<ResultT>(
        WorkerProto::Op::CollectGarbage,
        options.action,
        WorkerProto::write(*conn, options.pathsToDelete),
        options.ignoreLiveness,
        options.maxFreed,
        /* removed options */
        0, 0, 0
    ));

    {
        auto state_(co_await Store::state.lock());
        state_->pathInfoCache.clear();
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::optimiseStore()
try {
    auto conn(TRY_AWAIT(getConnection()));
    TRY_AWAIT(conn.sendCommand<unsigned>(WorkerProto::Op::OptimiseStore));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<bool>> RemoteStore::verifyStore(bool checkContents, RepairFlag repair)
try {
    auto conn(TRY_AWAIT(getConnection()));
    co_return TRY_AWAIT(
        conn.sendCommand<unsigned>(WorkerProto::Op::VerifyStore, checkContents, repair)
    );
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>>
RemoteStore::addSignatures(const StorePath & storePath, const StringSet & sigs)
try {
    auto conn(TRY_AWAIT(getConnection()));
    TRY_AWAIT(
        conn.sendCommand<unsigned>(WorkerProto::Op::AddSignatures, printStorePath(storePath), sigs)
    );
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::queryMissing(const std::vector<DerivedPath> & targets,
    StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
    uint64_t & downloadSize, uint64_t & narSize)
try {
    auto conn(TRY_AWAIT(getConnection()));
    using ResultT = std::tuple<StorePathSet, StorePathSet, StorePathSet, uint64_t, uint64_t>;
    std::tie(willBuild, willSubstitute, unknown, downloadSize, narSize) = TRY_AWAIT(
        conn.sendCommand<ResultT>(WorkerProto::Op::QueryMissing, WorkerProto::write(*conn, targets))
    );
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::addBuildLog(const StorePath & drvPath, std::string_view log)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::AddBuildLog << drvPath.to_string();
    AsyncStringInputStream source(log);
    TRY_AWAIT(conn.withFramedSinkAsync([&](Sink & sink) {
        return source.drainInto(sink);
    }));
    readInt(conn->from);
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<std::optional<std::string>>> RemoteStore::getVersion()
try {
    auto conn(TRY_AWAIT(getConnection()));
    co_return conn->daemonNixVersion;
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::connect()
try {
    auto conn(TRY_AWAIT(getConnection()));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<unsigned int>> RemoteStore::getProtocol()
try {
    auto conn(TRY_AWAIT(connections->get()));
    co_return conn->daemonVersion;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<TrustedFlag>>> RemoteStore::isTrustedClient()
try {
    auto conn(TRY_AWAIT(getConnection()));
    co_return conn->remoteTrustsUs;
} catch (...) {
    co_return result::current_exception();
}


RemoteStore::Connection::~Connection()
{
    try {
        to.flush();
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

kj::Promise<Result<box_ptr<Source>>> RemoteStore::narFromPath(const StorePath & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    TRY_AWAIT(conn.sendCommand(WorkerProto::Op::NarFromPath, printStorePath(path)));
    co_return make_box_ptr<GeneratorSource>([](auto conn) -> WireFormatGenerator {
        co_yield copyNAR(conn->from);
    }(std::move(conn)));
} catch (...) {
    co_return result::current_exception();
}


ref<FSAccessor> RemoteStore::getFSAccessor()
{
    return make_ref<RemoteFSAccessor>(ref<Store>(*this));
}

static Logger::Fields readFields(Source & from)
{
    Logger::Fields fields;
    size_t size = readInt(from);
    for (size_t n = 0; n < size; n++) {
        auto type = (decltype(Logger::Field::type)) readInt(from);
        if (type == Logger::Field::tInt)
            fields.push_back(readNum<uint64_t>(from));
        else if (type == Logger::Field::tString)
            fields.push_back(readString(from));
        else
            throw Error("got unsupported field type %x from Nix daemon", (int) type);
    }
    return fields;
}


std::exception_ptr RemoteStore::Connection::processStderr(bool flush)
{
    if (flush)
        to.flush();

    while (true) {

        auto msg = readNum<uint64_t>(from);

        if (msg == STDERR_ERROR) {
            return std::make_exception_ptr(readError(from));
        }

        else if (msg == STDERR_NEXT)
            printError(chomp(readString(from)));

        else if (msg == STDERR_START_ACTIVITY) {
            auto act = readNum<ActivityId>(from);
            auto lvl = (Verbosity) readInt(from);
            auto type = (ActivityType) readInt(from);
            auto s = readString(from);
            auto fields = readFields(from);
            auto parent = readNum<ActivityId>(from);
            logger->startActivity(act, lvl, type, s, fields, parent);
        }

        else if (msg == STDERR_STOP_ACTIVITY) {
            auto act = readNum<ActivityId>(from);
            logger->stopActivity(act);
        }

        else if (msg == STDERR_RESULT) {
            auto act = readNum<ActivityId>(from);
            auto type = (ResultType) readInt(from);
            auto fields = readFields(from);
            logger->result(act, type, fields);
        }

        else if (msg == STDERR_LAST)
            break;

        else
            throw Error("got unknown message type %x from Nix daemon", msg);
    }

    return nullptr;
}

RemoteStore::ConnectionHandle::FramedSinkHandler::FramedSinkHandler(
    ConnectionHandle & conn, ThreadPool & handlerThreads
)
    : stderrHandler([&]() {
        try {
            conn.processStderr(false);
        } catch (...) {
            ex = std::current_exception();
        }
    })
{
    conn.handle->to.flush();
    handlerThreads.enqueue([&] { stderrHandler(); });
}

RemoteStore::ConnectionHandle::FramedSinkHandler::~FramedSinkHandler() noexcept(false)
{
    stderrHandler.get_future().get();
    // if we're handling an Interrupted exception we must be careful: it's
    // possible that the exception was thrown by the withFramedSink framed
    // function, but not by the FramedSink itself. in this case our stderr
    // handler thread may race with FramedSink::writeUnbuffered, catch the
    // Interrupted exception independently, store it into ex, and have our
    // own destructor rethrow a second copy of Interrupted. since we can't
    // handle multiple exceptions anyway the safest path is to simply drop
    // the remote (possibly Interrupted) exception when called for unwind.
    if (ex && std::uncaught_exceptions() == 0) {
        std::rethrow_exception(ex);
    }
}

kj::Promise<Result<void>> RemoteStore::ConnectionHandle::withFramedSinkAsync(
    std::function<kj::Promise<Result<void>>(Sink & sink)> fun
)
try {
    {
        FramedSinkHandler handler{*this, *handlerThreads.lock()};
        FramedSink sink((*this)->to, handler.ex);
        TRY_AWAIT(fun(sink));
        sink.flush();
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

}
