#include "lix/libutil/async-collect.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise-async.hh"
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
#include "lix/libutil/types.hh"
#include "path-info.hh"

#include <cstdint>
#include <exception>
#include <kj/async.h>
#include <kj/common.h>
#include <optional>
#include <string_view>
#include <utility>

namespace nix {

/* TODO: Separate these store impls into different files, give them better names */
RemoteStore::RemoteStore(const RemoteStoreConfig & config)
    : Store(config)
    , connections(make_ref<Pool<Connection>>(
          std::max(1, (int) config.maxConnections),
          [this]() { return openAndInitConnection(); },
          [this](const ref<Connection> & r) {
              return std::chrono::duration_cast<std::chrono::seconds>(
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
        TRY_AWAIT(initConnection(*conn));
        co_return conn;
    } catch (...) {
        failed = true;
        throw;
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> RemoteStore::initConnection(Connection & conn)
try {
    /* Send the magic greeting, check for the reply. */
    try {
        AsyncFdIoStream stream{AsyncFdIoStream::shared_fd{}, conn.getFD()};
        AsyncBufferedInputStream from{stream, conn.fromBuf};

        conn.store = this;
        {
            StringSink packet;
            packet << WORKER_MAGIC_1;
            TRY_AWAIT(stream.writeFull(packet.s.data(), packet.s.size()));
        }

        uint64_t magic = TRY_AWAIT(readNum<uint64_t>(from));
        if (magic != WORKER_MAGIC_2)
            throw Error("protocol mismatch");

        conn.daemonVersion = TRY_AWAIT(readNum<unsigned>(from));
        if (GET_PROTOCOL_MAJOR(conn.daemonVersion) != GET_PROTOCOL_MAJOR(PROTOCOL_VERSION))
            throw Error("Nix daemon protocol version not supported");
        if (GET_PROTOCOL_MINOR(conn.daemonVersion) < MIN_SUPPORTED_MINOR_WORKER_PROTO_VERSION)
            throw Error("The remote Nix daemon version is too old");

        {
            StringSink packet;
            packet << PROTOCOL_VERSION;
            packet << 0;     // Obsolete CPU affinity.
            packet << false; // obsolete reserveSpace
            TRY_AWAIT(stream.writeFull(packet.s.data(), packet.s.size()));
        }

        conn.daemonNixVersion = TRY_AWAIT(readString(from));
        conn.remoteTrustsUs = TRY_AWAIT(WorkerProto::readAsync(
            from,
            *conn.store,
            conn.daemonVersion,
            WorkerProto::Serialise<std::optional<TrustedFlag>>::read
        ));

        auto ex = TRY_AWAIT(conn.processStderr(stream));
        if (ex.e) {
            std::rethrow_exception(ex.e);
        }
    }
    catch (Error & e) {
        throw Error("cannot open connection to remote store '%s': %s", getUri(), e.what());
    }

    TRY_AWAIT(setOptions(conn));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> RemoteStore::setOptions(Connection & conn)
try {
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

    AsyncFdIoStream stream{AsyncFdIoStream::shared_fd{}, conn.getFD()};
    TRY_AWAIT(stream.writeFull(command.s.data(), command.s.size()));
    auto ex = TRY_AWAIT(conn.processStderr(stream));
    if (ex.e) {
        std::rethrow_exception(ex.e);
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> RemoteStore::ConnectionHandle::processStderr(AsyncFdIoStream & stream)
try {
    auto ex = TRY_AWAIT(handle->processStderr(stream));
    if (ex.e) {
        co_return result::failure(ex.e);
    }
    co_return result::success();
} catch (...) {
    handle.markBad();
    co_return result::current_exception();
}

kj::Promise<Result<RemoteStore::ConnectionHandle>> RemoteStore::getConnection()
try {
    co_return ConnectionHandle(TRY_AWAIT(connections->get()));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> RemoteStore::setOptions()
try {
    TRY_AWAIT(setOptions(*(TRY_AWAIT(getConnection()).handle)));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<bool>>
RemoteStore::isValidPathUncached(const StorePath & path, const Activity * context)
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
RemoteStore::queryPathInfoUncached(const StorePath & path, const Activity * context)
try {
    auto conn(TRY_AWAIT(getConnection()));
    std::optional<UnkeyedValidPathInfo> pathInfo;
    try {
        pathInfo = TRY_AWAIT(conn.sendCommand<std::optional<UnkeyedValidPathInfo>>(
            WorkerProto::Op::QueryPathInfo, printStorePath(path)
        ));
        if (!pathInfo) {
            co_return result::success(nullptr);
        }
    } catch (Error & e) {
        // Ugly backwards compatibility hack. TODO(fj#325): remove.
        if (e.msg().find("is not valid") != std::string::npos)
            co_return result::success(nullptr);
        throw;
    }

    co_return std::make_shared<ValidPathInfo>(StorePath{path}, std::move(*pathInfo));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::queryReferrers(const StorePath & path,
    StorePathSet & referrers)
try {
    auto conn(TRY_AWAIT(getConnection()));
    referrers.merge(TRY_AWAIT(
        conn.sendCommand<StorePathSet>(WorkerProto::Op::QueryReferrers, printStorePath(path))
    ));
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

    // The dump source may invoke the store, so we need to make some room.
    connections->incCapacity();
    Finally cleanup([&]() { connections->decCapacity(); });

    co_return make_ref<ValidPathInfo>(TRY_AWAIT(conn.sendCommand<ValidPathInfo>(
        WorkerProto::Op::AddToStore,
        name,
        caMethod.render(hashType),
        WorkerProto::write(*conn, references),
        repair,
        [&](AsyncOutputStream & stream) { return dump.drainInto(stream); }
    )));
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
    CheckSigsFlag checkSigs,
    const Activity * context
)
try {
    auto conn(TRY_AWAIT(getConnection()));

    auto copier = copyNAR(source);
    TRY_AWAIT(conn.sendCommand(
        WorkerProto::Op::AddToStoreNar,
        printStorePath(info.path),
        (info.deriver ? printStorePath(*info.deriver) : ""),
        info.narHash.to_string(Base::Base16, false),
        WorkerProto::write(*conn, info.references),
        info.registrationTime,
        info.narSize,
        info.ultimate,
        info.sigs,
        renderContentAddress(info.ca),
        repair,
        !checkSigs,
        [&](AsyncOutputStream & stream) { return copier->drainInto(stream); }
    ));
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
    TRY_AWAIT(conn.sendCommand(
        WorkerProto::Op::AddMultipleToStore,
        repair,
        !checkSigs,
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        [&](AsyncOutputStream & stream) -> kj::Promise<Result<void>> {
            try {
                auto send = [&]<typename T>(T && value) {
                    auto tmp = make_box_ptr<StringSink>();
                    *tmp << std::forward<T>(value);
                    return stream.writeFull(tmp->s.data(), tmp->s.size()).attach(std::move(tmp));
                };

                TRY_AWAIT(send(pathsToCopy.size()));
                for (auto & [pathInfo, pathSource] : pathsToCopy) {
                    TRY_AWAIT(send(WorkerProto::Serialise<ValidPathInfo>::write(
                        WorkerProto::WriteConn{*this, remoteVersion}, pathInfo
                    )));
                    TRY_AWAIT(TRY_AWAIT(pathSource())->drainInto(stream));
                }
                co_return result::success();
            } catch (...) {
                co_return result::current_exception();
            }
        }
    ));
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
    AsyncStringInputStream source(log);
    TRY_AWAIT(conn.sendCommand<unsigned>(
        WorkerProto::Op::AddBuildLog,
        drvPath.to_string(),
        [&](AsyncOutputStream & stream) { return source.drainInto(stream); }
    ));
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

kj::Promise<Result<box_ptr<AsyncInputStream>>>
RemoteStore::narFromPath(const StorePath & path, const Activity * context)
try {
    struct NarStream : AsyncInputStream
    {
        ConnectionHandle conn;
        AsyncFdIoStream rawStream;
        AsyncBufferedInputStream bufferedIn;
        box_ptr<AsyncInputStream> narCopier;
        NarStream(ConnectionHandle conn)
            : conn(std::move(conn))
            , rawStream(AsyncFdIoStream::shared_fd{}, this->conn->getFD())
            , bufferedIn(this->rawStream, this->conn->fromBuf)
            , narCopier(copyNAR(bufferedIn))
        {
        }

        kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) override
        {
            return narCopier->read(buffer, size);
        }
    };

    auto conn(TRY_AWAIT(getConnection()));
    TRY_AWAIT(conn.sendCommand(WorkerProto::Op::NarFromPath, printStorePath(path)));
    co_return make_box_ptr<NarStream>(std::move(conn));
} catch (...) {
    co_return result::current_exception();
}


ref<FSAccessor> RemoteStore::getFSAccessor()
{
    return make_ref<RemoteFSAccessor>(ref<Store>(*this));
}

static kj::Promise<Result<Logger::Fields>> readFields(AsyncInputStream & from)
try {
    Logger::Fields fields;
    size_t size = TRY_AWAIT(readNum<unsigned>(from));
    for (size_t n = 0; n < size; n++) {
        auto type = (decltype(Logger::Field::type)) TRY_AWAIT(readNum<unsigned>(from));
        if (type == Logger::Field::tInt)
            fields.push_back(TRY_AWAIT(readNum<uint64_t>(from)));
        else if (type == Logger::Field::tString)
            fields.push_back(TRY_AWAIT(readString(from)));
        else
            throw Error("got unsupported field type %x from Nix daemon", (int) type);
    }
    co_return fields;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<RemoteStore::Connection::RemoteError>>
RemoteStore::Connection::processStderr(AsyncFdIoStream & stream)
try {
    AsyncBufferedInputStream from{stream, fromBuf};

    while (true) {
        auto msg = TRY_AWAIT(readNum<uint64_t>(from));

        if (msg == STDERR_ERROR) {
            co_return RemoteError{std::make_exception_ptr(TRY_AWAIT(readError(from)))};
        }

        else if (msg == STDERR_NEXT)
            printError("%1%", Uncolored(chomp(TRY_AWAIT(readString(from)))));

        else if (msg == STDERR_START_ACTIVITY) {
            auto act = TRY_AWAIT(readNum<ActivityId>(from));
            auto lvl = (Verbosity) TRY_AWAIT(readNum<unsigned>(from));
            auto type = (ActivityType) TRY_AWAIT(readNum<unsigned>(from));
            auto s = TRY_AWAIT(readString(from));
            auto fields = TRY_AWAIT(readFields(from));
            auto parent = TRY_AWAIT(readNum<ActivityId>(from));
            logger->startActivity(act, lvl, type, s, fields, parent);
        }

        else if (msg == STDERR_STOP_ACTIVITY) {
            auto act = TRY_AWAIT(readNum<ActivityId>(from));
            logger->stopActivity(act);
        }

        else if (msg == STDERR_RESULT) {
            auto act = TRY_AWAIT(readNum<ActivityId>(from));
            auto type = (ResultType) TRY_AWAIT(readNum<unsigned>(from));
            auto fields = TRY_AWAIT(readFields(from));
            logger->result(act, type, fields);
        }

        else if (msg == STDERR_LAST)
            break;

        else
            throw Error("got unknown message type %x from Nix daemon", msg);
    }

    co_return RemoteError{nullptr};
} catch (SerialisationError & e) {
    co_return result::failure(
        std::make_exception_ptr(Error("error reading daemon response: %s", e.what()))
    );
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> RemoteStore::ConnectionHandle::withFramedStream(
    AsyncFdIoStream & stream,
    std::function<kj::Promise<Result<void>>(AsyncOutputStream & stream)> fun
)
try {
    AsyncFramedOutputStream framed(stream);
    AsyncBufferedOutputStream sink(framed);

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    auto send = [&]() -> kj::Promise<Result<void>> {
        try {
            TRY_AWAIT(fun(sink));
            TRY_AWAIT(sink.flush());
            TRY_AWAIT(framed.finish());
            co_return result::success();
        } catch (...) {
            handle.markBad();
            co_return result::current_exception();
        }
    };

    TRY_AWAIT(asyncJoin(send(), processStderr(stream)));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}
}
