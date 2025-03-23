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

#include <kj/async.h>
#include <optional>
#include <utility>

namespace nix {

/* TODO: Separate these store impls into different files, give them better names */
RemoteStore::RemoteStore(const RemoteStoreConfig & config)
    : Store(config)
    , connections(make_ref<Pool<Connection>>(
            std::max(1, (int) config.maxConnections),
            [this]() {
                auto conn = openConnectionWrapper();
                try {
                    initConnection(*conn);
                } catch (...) {
                    failed = true;
                    throw;
                }
                return conn;
            },
            [this](const ref<Connection> & r) {
                return
                    r->to.good()
                    && r->from.good()
                    && std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - r->startTime).count() < this->config().maxConnectionAge;
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


void RemoteStore::initConnection(Connection & conn)
{
    /* Send the magic greeting, check for the reply. */
    try {
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

        if (GET_PROTOCOL_MINOR(conn.daemonVersion) >= 33) {
            conn.to.flush();
            conn.daemonNixVersion = readString(conn.from);
        }

        if (GET_PROTOCOL_MINOR(conn.daemonVersion) >= 35) {
            conn.remoteTrustsUs = WorkerProto::Serialise<std::optional<TrustedFlag>>::read(*this, conn);
        } else {
            // We don't know the answer; protocol to old.
            conn.remoteTrustsUs = std::nullopt;
        }

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
    conn.to << WorkerProto::Op::SetOptions
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
    conn.to << overrides.size();
    for (auto & i : overrides)
        conn.to << i.first << i.second.value;

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

void RemoteStore::ConnectionHandle::processStderr(Sink * sink, Source * source, bool flush)
{
    auto ex = handle->processStderr(sink, source, flush);
    if (ex) {
        daemonException = true;
        try {
            std::rethrow_exception(ex);
        } catch (const Error & e) {
            // Nix versions before #4628 did not have an adequate behavior for reporting that the derivation format was upgraded.
            // To avoid having to add compatibility logic in many places, we expect to catch almost all occurrences of the
            // old incomprehensible error here, so that we can explain to users what's going on when their daemon is
            // older than #4628 (2023).
            if (experimentalFeatureSettings.isEnabled(Xp::DynamicDerivations) &&
                GET_PROTOCOL_MINOR(handle->daemonVersion) <= 35)
            {
                auto m = e.msg();
                if (m.find("parsing derivation") != std::string::npos &&
                    m.find("expected string") != std::string::npos &&
                    m.find("Derive([") != std::string::npos)
                    throw Error("%s, this might be because the daemon is too old to understand dependencies on dynamic derivations. Check to see if the raw derivation is in the form '%s'", std::move(m), "DrvWithVersion(..)");
            }
            throw;
        }
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
    conn->to << WorkerProto::Op::IsValidPath << printStorePath(path);
    conn.processStderr();
    co_return readInt(conn->from);
} catch (...) {
    co_return result::current_exception();
}


kj ::Promise<Result<StorePathSet>>
RemoteStore::queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::QueryValidPaths;
    conn->to << WorkerProto::write(*this, *conn, paths);
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 27) {
        conn->to << maybeSubstitute;
    }
    conn.processStderr();
    co_return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePathSet>> RemoteStore::queryAllValidPaths()
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::QueryAllValidPaths;
    conn.processStderr();
    co_return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePathSet>> RemoteStore::querySubstitutablePaths(const StorePathSet & paths)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::QuerySubstitutablePaths;
    conn->to << WorkerProto::write(*this, *conn, paths);
    conn.processStderr();
    co_return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::querySubstitutablePathInfos(const StorePathCAMap & pathsMap, SubstitutablePathInfos & infos)
try {
    if (pathsMap.empty()) co_return result::success();

    auto conn(TRY_AWAIT(getConnection()));


    conn->to << WorkerProto::Op::QuerySubstitutablePathInfos;
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 22) {
        StorePathSet paths;
        for (auto & path : pathsMap)
            paths.insert(path.first);
        conn->to << WorkerProto::write(*this, *conn, paths);
    } else
        conn->to << WorkerProto::write(*this, *conn, pathsMap);
    conn.processStderr();
    size_t count = readNum<size_t>(conn->from);
    for (size_t n = 0; n < count; n++) {
        SubstitutablePathInfo & info(infos[parseStorePath(readString(conn->from))]);
        auto deriver = readString(conn->from);
        if (deriver != "")
            info.deriver = parseStorePath(deriver);
        info.references = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
        info.downloadSize = readLongLong(conn->from);
        info.narSize = readLongLong(conn->from);
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<std::shared_ptr<const ValidPathInfo>>>
RemoteStore::queryPathInfoUncached(const StorePath & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::QueryPathInfo << printStorePath(path);
    try {
        conn.processStderr();
    } catch (Error & e) {
        // Ugly backwards compatibility hack. TODO(fj#325): remove.
        if (e.msg().find("is not valid") != std::string::npos)
            co_return result::success(nullptr);
        throw;
    }

    bool valid; conn->from >> valid;
    if (!valid) co_return result::success(nullptr);

    co_return std::make_shared<ValidPathInfo>(
        StorePath{path},
        WorkerProto::Serialise<UnkeyedValidPathInfo>::read(*this, *conn));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::queryReferrers(const StorePath & path,
    StorePathSet & referrers)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::QueryReferrers << printStorePath(path);
    conn.processStderr();
    for (auto & i : WorkerProto::Serialise<StorePathSet>::read(*this, *conn))
        referrers.insert(i);
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePathSet>> RemoteStore::queryValidDerivers(const StorePath & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::QueryValidDerivers << printStorePath(path);
    conn.processStderr();
    co_return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePathSet>> RemoteStore::queryDerivationOutputs(const StorePath & path)
try {
    if (GET_PROTOCOL_MINOR(TRY_AWAIT(getProtocol())) >= 22) {
        co_return TRY_AWAIT(Store::queryDerivationOutputs(path));
    }
    REMOVE_AFTER_DROPPING_PROTO_MINOR(21);
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::QueryDerivationOutputs << printStorePath(path);
    conn.processStderr();
    co_return WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
} catch (...) {
    co_return result::current_exception();
}


kj ::Promise<Result<std::map<std::string, std::optional<StorePath>>>>
RemoteStore::queryPartialDerivationOutputMap(const StorePath & path, Store * evalStore_)
try {
    if (GET_PROTOCOL_MINOR(TRY_AWAIT(getProtocol())) >= 22) {
        if (!evalStore_) {
            auto conn(TRY_AWAIT(getConnection()));
            conn->to << WorkerProto::Op::QueryDerivationOutputMap << printStorePath(path);
            conn.processStderr();
            co_return WorkerProto::Serialise<std::map<std::string, std::optional<StorePath>>>::read(
                *this, *conn
            );
        } else {
            auto & evalStore = *evalStore_;
            auto outputs = TRY_AWAIT(evalStore.queryStaticPartialDerivationOutputMap(path));
            // union with the first branch overriding the statically-known ones
            // when non-`std::nullopt`.
            for (auto && [outputName, optPath] :
                 TRY_AWAIT(queryPartialDerivationOutputMap(path, nullptr)))
            {
                if (optPath)
                    outputs.insert_or_assign(std::move(outputName), std::move(optPath));
                else
                    outputs.insert({std::move(outputName), std::nullopt});
            }
            co_return outputs;
        }
    } else {
        REMOVE_AFTER_DROPPING_PROTO_MINOR(21);
        auto & evalStore = evalStore_ ? *evalStore_ : *this;
        // Fallback for old daemon versions.
        // For floating-CA derivations (and their co-dependencies) this is an
        // under-approximation as it only returns the paths that can be inferred
        // from the derivation itself (and not the ones that are known because
        // the have been built), but as old stores don't handle floating-CA
        // derivations this shouldn't matter
        co_return TRY_AWAIT(evalStore.queryStaticPartialDerivationOutputMap(path));
    }
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<StorePath>>>
RemoteStore::queryPathFromHashPart(const std::string & hashPart)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::QueryPathFromHashPart << hashPart;
    conn.processStderr();
    Path path = readString(conn->from);
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
    std::optional<ConnectionHandle> conn_(TRY_AWAIT(getConnection()));
    auto & conn = *conn_;

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 25) {

        conn->to
            << WorkerProto::Op::AddToStore
            << name
            << caMethod.render(hashType);
        conn->to << WorkerProto::write(*this, *conn, references);
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
            WorkerProto::Serialise<ValidPathInfo>::read(*this, *conn));
    }
    else {
        if (repair) throw Error("repairing is not supported when building through the Nix daemon protocol < 1.25");

        auto handlers = overloaded{
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
            [&](const TextIngestionMethod & thm) -> kj::Promise<Result<void>> {
                try {
                    if (hashType != HashType::SHA256)
                        throw UnimplementedError("When adding text-hashed data called '%s', only SHA-256 is supported but '%s' was given",
                            name, printHashType(hashType));
                    std::string s = TRY_AWAIT(dump.drain());
                    conn->to << WorkerProto::Op::AddTextToStore << name << s;
                    conn->to << WorkerProto::write(*this, *conn, references);
                    conn.processStderr();
                    co_return result::success();
                } catch (...) {
                    co_return result::current_exception();
                }
            },
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
            [&](const FileIngestionMethod & fim) -> kj::Promise<Result<void>> {
                try {
                    conn->to
                    << WorkerProto::Op::AddToStore
                    << name
                    << ((hashType == HashType::SHA256 && fim == FileIngestionMethod::Recursive) ? 0 : 1) /* backwards compatibility hack */
                    << (fim == FileIngestionMethod::Recursive ? 1 : 0)
                    << printHashType(hashType);

                    try {
                        conn->to.written = 0;
                        connections->incCapacity();
                        {
                            Finally cleanup([&]() { connections->decCapacity(); });
                            if (fim == FileIngestionMethod::Recursive) {
                                TRY_AWAIT(dump.drainInto(conn->to));
                            } else {
                                std::string contents = TRY_AWAIT(dump.drain());
                                conn->to << dumpString(contents);
                            }
                        }
                        conn.processStderr();
                    } catch (SysError & e) {
                        /* Daemon closed while we were sending the path. Probably OOM
                           or I/O error. */
                        if (e.errNo == EPIPE)
                            try {
                                conn.processStderr();
                            } catch (EndOfFile & e) { }
                        throw;
                    }

                    co_return result::success();
                } catch (...) {
                    co_return result::current_exception();
                }
            }
        };
        TRY_AWAIT(std::visit(handlers, caMethod.raw));
        auto path = parseStorePath(readString(conn->from));
        // Release our connection to prevent a deadlock in queryPathInfo().
        conn_.reset();
        co_return TRY_AWAIT(queryPathInfo(path));
    }
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
    conn->to << WorkerProto::write(*this, *conn, info.references);
    conn->to << info.registrationTime << info.narSize
             << info.ultimate << info.sigs << renderContentAddress(info.ca)
             << repair << !checkSigs;

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 23) {
        auto copier = copyNAR(source);
        TRY_AWAIT(conn.withFramedSinkAsync([&](Sink & sink) {
            return copier->drainInto(sink);
        }));
    } else {
        IndirectAsyncInputStreamToSource is(source);
        auto pfp = kj::newPromiseAndCrossThreadFulfiller<void>();
        auto thread = std::async(std::launch::async, [&] {
            KJ_DEFER(pfp.fulfiller->fulfill());
            conn.processStderr(0, &is);
        });
        co_await pfp.promise.exclusiveJoin(is.feed());
        // if the thread stops we're always clear. if the feeder stops early (or
        // fails) it'll have thrown an exception, and the thread will stop soon.
        thread.get();
    }
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
    if (GET_PROTOCOL_MINOR(TRY_AWAIT(getConnection())->daemonVersion) >= 32) {
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
                    sink << WorkerProto::Serialise<ValidPathInfo>::write(*this,
                        WorkerProto::WriteConn {remoteVersion},
                        pathInfo);
                    TRY_AWAIT(TRY_AWAIT(pathSource())->drainInto(sink));
                }
                co_return result::success();
            } catch (...) {
                co_return result::current_exception();
            }
        }));
    } else {
        for (auto & [pathInfo, pathSource] : pathsToCopy) {
            pathInfo.ultimate = false; // duplicated in daemon.cc AddMultipleToStore
            TRY_AWAIT(addToStore(pathInfo, *TRY_AWAIT(pathSource()), repair, checkSigs));
        }
    }
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

kj::Promise<Result<void>> RemoteStore::registerDrvOutput(const Realisation & info)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::RegisterDrvOutput;
    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 31) {
        REMOVE_AFTER_DROPPING_PROTO_MINOR(30);
        conn->to << info.id.to_string();
        conn->to << std::string(info.outPath.to_string());
    } else {
        conn->to << WorkerProto::write(*this, *conn, info);
    }
    conn.processStderr();
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::shared_ptr<const Realisation>>>
RemoteStore::queryRealisationUncached(const DrvOutput & id)
try {
    auto conn(TRY_AWAIT(getConnection()));

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 27) {
        warn("the daemon is too old to support content-addressed derivations, please upgrade it to 2.4");
        co_return result::success(nullptr);
    }

    conn->to << WorkerProto::Op::QueryRealisation;
    conn->to << id.to_string();
    conn.processStderr();

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) < 31) {
        auto outPaths = WorkerProto::Serialise<std::set<StorePath>>::read(
            *this, *conn);
        if (outPaths.empty())
            co_return result::success(nullptr);
        co_return std::make_shared<const Realisation>(
            Realisation{.id = id, .outPath = *outPaths.begin()}
        );
    } else {
        auto realisations = WorkerProto::Serialise<std::set<Realisation>>::read(
            *this, *conn);
        if (realisations.empty())
            co_return result::success(nullptr);
        co_return std::make_shared<const Realisation>(*realisations.begin());
    }
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
                    drvPaths2.insert(bp.drvPath->getBaseStorePath());
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
    conn->to << WorkerProto::Op::BuildPaths;
    conn->to << WorkerProto::write(*this, *conn, drvPaths);
    conn->to << buildMode;
    conn.processStderr();
    readInt(conn->from);
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

    std::optional<ConnectionHandle> conn_(TRY_AWAIT(getConnection()));
    auto & conn = *conn_;

    if (GET_PROTOCOL_MINOR(conn->daemonVersion) >= 34) {
        conn->to << WorkerProto::Op::BuildPathsWithResults;
        conn->to << WorkerProto::write(*this, *conn, paths);
        conn->to << buildMode;
        conn.processStderr();
        co_return WorkerProto::Serialise<std::vector<KeyedBuildResult>>::read(*this, *conn);
    } else {
        REMOVE_AFTER_DROPPING_PROTO_MINOR(33);
        // Avoid deadlock.
        conn_.reset();

        // Note: this throws an exception if a build/substitution
        // fails, but meh.
        TRY_AWAIT(buildPaths(paths, buildMode, evalStore));

        std::vector<KeyedBuildResult> results;

        for (auto & path : paths) {
            auto handlers = overloaded {
                [&](const DerivedPath::Opaque & bo) -> kj::Promise<Result<void>> {
                    try {
                        results.push_back(KeyedBuildResult {
                            {
                                .status = BuildResult::Substituted,
                            },
                            /* .path = */ bo,
                        });
                        return {result::success()};
                    } catch (...) {
                        return {result::current_exception()};
                    }
                },
                // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
                [&](const DerivedPath::Built & bfd) -> kj::Promise<Result<void>> {
                    try {
                        KeyedBuildResult res {
                            {
                                .status = BuildResult::Built
                            },
                            /* .path = */ bfd,
                        };

                        OutputPathMap outputs;
                        auto drvPath = TRY_AWAIT(resolveDerivedPath(*evalStore, *bfd.drvPath));
                        auto drv = TRY_AWAIT(evalStore->readDerivation(drvPath));
                        const auto outputHashes =
                            TRY_AWAIT(staticOutputHashes(*evalStore, drv)); // FIXME: expensive
                        auto built = TRY_AWAIT(resolveDerivedPath(*this, bfd, &*evalStore));
                        for (auto & [output, outputPath] : built) {
                            auto outputHash = get(outputHashes, output);
                            if (!outputHash)
                                throw Error(
                                    "the derivation '%s' doesn't have an output named '%s'",
                                    printStorePath(drvPath), output);
                            auto outputId = DrvOutput{ *outputHash, output };
                            if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
                                auto realisation =
                                    TRY_AWAIT(queryRealisation(outputId));
                                if (!realisation)
                                    throw MissingRealisation(outputId);
                                res.builtOutputs.emplace(output, *realisation);
                            } else {
                                res.builtOutputs.emplace(
                                    output,
                                    Realisation {
                                        .id = outputId,
                                        .outPath = outputPath,
                                    });
                            }
                        }

                        results.push_back(res);
                        co_return result::success();
                    } catch (...) {
                        co_return result::current_exception();
                    }
                }
            };
            TRY_AWAIT(std::visit(handlers , path.raw()));
        }

        co_return results;
    }
} catch (...) {
    co_return result::current_exception();
}

kj ::Promise<Result<BuildResult>> RemoteStore::buildDerivation(
    const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode
)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::BuildDerivation << printStorePath(drvPath);
    writeDerivation(conn->to, *this, drv);
    conn->to << buildMode;
    conn.processStderr();
    co_return WorkerProto::Serialise<BuildResult>::read(*this, *conn);
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::ensurePath(const StorePath & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::EnsurePath << printStorePath(path);
    conn.processStderr();
    readInt(conn->from);
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::addTempRoot(const StorePath & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::AddTempRoot << printStorePath(path);
    conn.processStderr();
    readInt(conn->from);
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Roots>> RemoteStore::findRoots(bool censor)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::FindRoots;
    conn.processStderr();
    size_t count = readNum<size_t>(conn->from);
    Roots result;
    while (count--) {
        Path link = readString(conn->from);
        auto target = parseStorePath(readString(conn->from));
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

    conn->to
        << WorkerProto::Op::CollectGarbage << options.action;
    conn->to << WorkerProto::write(*this, *conn, options.pathsToDelete);
    conn->to << options.ignoreLiveness
        << options.maxFreed
        /* removed options */
        << 0 << 0 << 0;

    conn.processStderr();

    results.paths = readStrings<PathSet>(conn->from);
    results.bytesFreed = readLongLong(conn->from);
    readLongLong(conn->from); // obsolete

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
    conn->to << WorkerProto::Op::OptimiseStore;
    conn.processStderr();
    readInt(conn->from);
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<bool>> RemoteStore::verifyStore(bool checkContents, RepairFlag repair)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::VerifyStore << checkContents << repair;
    conn.processStderr();
    co_return readInt(conn->from);
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>>
RemoteStore::addSignatures(const StorePath & storePath, const StringSet & sigs)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::AddSignatures << printStorePath(storePath) << sigs;
    conn.processStderr();
    readInt(conn->from);
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::queryMissing(const std::vector<DerivedPath> & targets,
    StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
    uint64_t & downloadSize, uint64_t & narSize)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::QueryMissing;
    conn->to << WorkerProto::write(*this, *conn, targets);
    conn.processStderr();
    willBuild = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
    willSubstitute = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
    unknown = WorkerProto::Serialise<StorePathSet>::read(*this, *conn);
    conn->from >> downloadSize >> narSize;
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> RemoteStore::addBuildLog(const StorePath & drvPath, std::string_view log)
try {
    auto conn(TRY_AWAIT(getConnection()));
    conn->to << WorkerProto::Op::AddBuildLog << drvPath.to_string();
    StringSource source(log);
    conn.withFramedSink([&](Sink & sink) {
        source.drainInto(sink);
    });
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
    auto conn(TRY_AWAIT(connections->get()));
    conn->to << WorkerProto::Op::NarFromPath << printStorePath(path);
    conn->processStderr();
    co_return make_box_ptr<GeneratorSource>([](auto conn) -> WireFormatGenerator {
        co_yield copyNAR(conn->from);
    }(std::move(conn)));
} catch (...) {
    co_return result::current_exception();
}


ref<FSAccessor> RemoteStore::getFSAccessor()
{
    return make_ref<RemoteFSAccessor>(ref<Store>(shared_from_this()));
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


std::exception_ptr RemoteStore::Connection::processStderr(Sink * sink, Source * source, bool flush)
{
    if (flush)
        to.flush();

    while (true) {

        auto msg = readNum<uint64_t>(from);

        if (msg == STDERR_WRITE) {
            auto s = readString(from);
            if (!sink) throw Error("no sink");
            (*sink)(s);
        }

        else if (msg == STDERR_READ) {
            if (!source) throw Error("no source");
            size_t len = readNum<size_t>(from);
            auto buf = std::make_unique<char[]>(len);
            to << std::string_view((const char *) buf.get(), source->read(buf.get(), len));
            to.flush();
        }

        else if (msg == STDERR_ERROR) {
            if (GET_PROTOCOL_MINOR(daemonVersion) >= 26) {
                return std::make_exception_ptr(readError(from));
            } else {
                auto error = readString(from);
                unsigned int status = readInt(from);
                return std::make_exception_ptr(Error(status, error));
            }
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
            conn.processStderr(nullptr, nullptr, false);
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

void RemoteStore::ConnectionHandle::withFramedSink(std::function<void(Sink & sink)> fun)
{
    FramedSinkHandler handler{*this, *handlerThreads.lock()};
    FramedSink sink((*this)->to, handler.ex);
    fun(sink);
    sink.flush();
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
