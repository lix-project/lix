#include "lix/libstore/legacy-ssh-store.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/pool.hh"
#include "lix/libstore/remote-store.hh"
#include "lix/libstore/serve-protocol.hh"
#include "lix/libstore/serve-protocol-impl.hh"
#include "lix/libstore/build-result.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libstore/ssh.hh"
#include "lix/libstore/ssh-store.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/strings.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libutil/config-impl.hh"
#include "lix/libutil/abstract-setting-to-json.hh"
#include "path-info.hh"
#include "path.hh"
#include <cstdint>
#include <optional>

namespace nix {

namespace {
struct QueryPathInfoResult
{
    std::string p;
    UnkeyedValidPathInfo info;
};

struct BuildPathsResult
{
    BuildResult result;
    std::optional<Error> error;
};
}

template<>
DECLARE_SERVE_SERIALISER(QueryPathInfoResult);
template<>
DECLARE_SERVE_SERIALISER(BuildPathsResult);

QueryPathInfoResult ServeProto::Serialise<QueryPathInfoResult>::read(ServeProto::ReadConn conn)
{
    auto p = readString(conn.from);
    if (p.empty()) {
        return {"", UnkeyedValidPathInfo{Hash::dummy}};
    }
    auto info = ServeProto::Serialise<UnkeyedValidPathInfo>::read(conn);

    if (info.narHash == Hash::dummy) {
        throw Error("NAR hash is now mandatory");
    }

    auto s = readString(conn.from);
    assert(s == "");
    return {std::move(p), std::move(info)};
}

BuildPathsResult ServeProto::Serialise<BuildPathsResult>::read(ServeProto::ReadConn conn)
{
    BuildResult result;
    result.status = (BuildResult::Status) readNum<unsigned>(conn.from);

    if (!result.success()) {
        result.errorMsg = readString(conn.from);
        return {result, Error(result.status, result.errorMsg)};
    }

    return {result, std::nullopt};
}

// writers are intentionally not defined

struct LegacySSHStoreConfig : CommonSSHStoreConfig
{
    using CommonSSHStoreConfig::CommonSSHStoreConfig;

    const Setting<Path> remoteProgram{this, "nix-store", "remote-program",
        "Path to the `nix-store` executable on the remote machine."};

    const Setting<int> maxConnections{this, 1, "max-connections",
        "Maximum number of concurrent SSH connections."};

    const std::string name() override { return "SSH Store"; }

    std::string doc() override
    {
        return
          #include "legacy-ssh-store.md"
          ;
    }
};

struct LegacySSHStoreConfigWithLog : LegacySSHStoreConfig
{
    using LegacySSHStoreConfig::LegacySSHStoreConfig;

    // Hack for getting remote build log output.
    // Intentionally not in `LegacySSHStoreConfig` so that it doesn't appear in
    // the documentation
    const Setting<int> logFD{this, -1, "log-fd", "file descriptor to which SSH's stderr is connected"};
};

struct LegacySSHStore final : public Store
{
    LegacySSHStoreConfigWithLog config_;

    LegacySSHStoreConfigWithLog & config() override { return config_; }
    const LegacySSHStoreConfigWithLog & config() const override { return config_; }

    struct Connection
    {
        ref<IoBuffer> fromBuf{make_ref<IoBuffer>()};
        std::unique_ptr<SSH::Connection> sshConn;
        ServeProto::Version remoteVersion;
        Store * store = nullptr;
        bool good = true;

        /*
         * Coercion to `ServeProto::WriteConn`. This makes it easy to use the
         * factored out serve protocol searlizers with a
         * `LegacySSHStore::Connection`.
         *
         * The serve protocol connection types are unidirectional, unlike
         * this type.
         */
        operator ServeProto::WriteConn ()
        {
            return ServeProto::WriteConn {
                .store = *store,
                .version = remoteVersion,
            };
        }

        template<typename Arg>
        kj::Promise<Result<void>>
        sendArg(AsyncOutputStream & stream, StringSink & buffer, Arg && arg)
        try {
            buffer << std::forward<Arg>(arg);
            return {result::success()};
        } catch (...) {
            return {result::current_exception()};
        }

        kj::Promise<Result<void>>
        sendArg(AsyncOutputStream & stream, StringSink & buffer, box_ptr<AsyncInputStream> && arg)
        try {
            TRY_AWAIT(stream.writeFull(buffer.s.data(), buffer.s.size()));
            buffer = {};
            TRY_AWAIT(arg->drainInto(stream));
            co_return result::success();
        } catch (...) {
            co_return result::current_exception();
        }

        template<typename R = void, typename... Args>
        kj::Promise<Result<R>> sendCommandUninterruptible(Args &&... args)
        try {
            // invalidate this connection if we're cancelled early, e.g. by a user ^C.
            // regular exceptions must be handled elsewhere due the subframe requests.
            // this also invalidates connections if a request was sent while unwinding
            // the stack, but that's sufficiently suspect to warrant being as careful.
            auto invalidateOnCancel = kj::defer([&] {
                if (std::uncaught_exceptions() == 0) {
                    good = false;
                }
            });

            AsyncFdIoStream stream(AsyncFdIoStream::shared_fd{}, sshConn->socket.get());

            {
                StringSink buffer;
                // can't use TRY_AWAIT here because macros break with variadic templates. sigh.
                ((co_await sendArg(stream, buffer, std::forward<Args>(args))).value(), ...);
                TRY_AWAIT(stream.writeFull(buffer.s.data(), buffer.s.size()));
            }

            if constexpr (std::is_void_v<R>) {
                invalidateOnCancel.cancel();
                co_return result::success();
            } else {
                AsyncBufferedInputStream from{stream, fromBuf};
                auto result = TRY_AWAIT(ServeProto::readAsync(
                    from, *store, remoteVersion, ServeProto::Serialise<R>::read
                ));
                invalidateOnCancel.cancel();
                co_return result;
            }
        } catch (...) {
            good = false;
            co_return result::current_exception();
        }

        template<typename R = void, typename... Args>
        kj::Promise<Result<R>> sendCommand(Args &&... args)
        {
            return makeInterruptible(sendCommandUninterruptible<R>(std::forward<Args>(args)...));
        }
    };

    std::string host;

    ref<Pool<Connection>> connections;

    SSH ssh;

    static std::set<std::string> uriSchemes() { return {"ssh"}; }

    LegacySSHStore(
        const std::string & scheme, const std::string & host, LegacySSHStoreConfigWithLog config
    )
        : Store(config)
        , config_(std::move(config))
        , host(host)
        , connections(make_ref<Pool<Connection>>(
            std::max(1, (int) config_.maxConnections),
            [this]() { return openConnection(); },
            [](const ref<Connection> & r) { return r->good; }
            ))
        , ssh(
            host,
            config_.port,
            config_.sshKey,
            config_.sshPublicHostKey,
            config_.compress,
            config_.logFD)
    {
    }

    kj::Promise<Result<ref<Connection>>> openConnection()
    try {
        auto conn = make_ref<Connection>();
        conn->sshConn = ssh.startCommand(
            fmt("%s --serve --write", config_.remoteProgram)
            + (config_.remoteStore.get() == ""
                   ? ""
                   : " --store " + shellEscape(config_.remoteStore.get()))
        );
        FdSink to(conn->sshConn->socket.get());
        FdSource from(conn->sshConn->socket.get(), conn->fromBuf);
        conn->store = this;

        try {
            to << SERVE_MAGIC_1 << SERVE_PROTOCOL_VERSION;
            to.flush();

            uint64_t magic = readNum<uint64_t>(from);
            if (magic != SERVE_MAGIC_2)
                throw Error("'nix-store --serve' protocol mismatch from '%s'", host);
            conn->remoteVersion = readNum<unsigned>(from);
            if (GET_PROTOCOL_MAJOR(conn->remoteVersion) != 0x200)
                throw Error("unsupported 'nix-store --serve' protocol version on '%s'", host);

            /* No longer support protocols this old*/
            if (GET_PROTOCOL_MINOR(conn->remoteVersion) < 4) {
                throw Error(
                    "remote '%s' is too old (protocol version %x)", host, conn->remoteVersion
                );
            }

        } catch (EndOfFile & e) {
            throw Error("cannot connect to '%1%'", host);
        }

        return {conn};
    } catch (...) {
        return {result::current_exception()};
    };

    std::string getUri() override
    {
        return *uriSchemes().begin() + "://" + host;
    }

    kj::Promise<Result<std::shared_ptr<const ValidPathInfo>>>
    queryPathInfoUncached(const StorePath & path, const Activity * context) override
    try {
        auto conn(TRY_AWAIT(connections->get()));

        debug("querying remote host '%s' for info on '%s'", host, printStorePath(path));

        auto result = TRY_AWAIT(conn->sendCommand<QueryPathInfoResult>(
            ServeProto::Command::QueryPathInfos, PathSet{printStorePath(path)}
        ));

        if (result.p.empty()) {
            co_return result::success(nullptr);
        }
        auto path2 = parseStorePath(result.p);
        assert(path == path2);
        co_return std::make_shared<ValidPathInfo>(path, std::move(result.info));
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> addToStore(
        const ValidPathInfo & info,
        AsyncInputStream & source,
        RepairFlag repair,
        CheckSigsFlag checkSigs,
        const Activity * context
    ) override
    try {
        debug("adding path '%s' to remote host '%s'", printStorePath(info.path), host);

        auto conn(TRY_AWAIT(connections->get()));
        unsigned result;

        if (GET_PROTOCOL_MINOR(conn->remoteVersion) >= 5) {
            result = TRY_AWAIT(conn->sendCommand<unsigned>(
                ServeProto::Command::AddToStoreNar,
                printStorePath(info.path),
                (info.deriver ? printStorePath(*info.deriver) : ""),
                info.narHash.to_string(Base::Base16, false),
                ServeProto::write(*conn, info.references),
                info.registrationTime,
                info.narSize,
                info.ultimate,
                info.sigs,
                renderContentAddress(info.ca),
                copyNAR(source)
            ));
        } else {
            result = TRY_AWAIT(conn->sendCommand<unsigned>(
                ServeProto::Command::ImportPaths,
                1,
                copyNAR(source),
                exportMagic,
                printStorePath(info.path),
                ServeProto::write(*conn, info.references),
                (info.deriver ? printStorePath(*info.deriver) : ""),
                0,
                0
            ));
        }

        if (result != 1) {
            throw Error(
                "failed to add path '%s' to remote host '%s'", printStorePath(info.path), host
            );
        }
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<box_ptr<AsyncInputStream>>>
    narFromPath(const StorePath & path, const Activity * context) override
    try {
        auto conn(TRY_AWAIT(connections->get()));

        struct NarStream : AsyncInputStream
        {
            Pool<Connection>::Handle conn;
            AsyncFdIoStream stream{AsyncFdIoStream::shared_fd{}, conn->sshConn->socket.get()};
            AsyncBufferedInputStream buffered{stream, conn->fromBuf};
            box_ptr<AsyncInputStream> copier{copyNAR(buffered)};

            NarStream(Pool<Connection>::Handle conn) : conn(std::move(conn)) {}

            kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) override
            {
                return copier->read(buffer, size);
            }
        };

        TRY_AWAIT(conn->sendCommand(ServeProto::Command::DumpStorePath, printStorePath(path)));
        co_return make_box_ptr<NarStream>(std::move(conn));
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<std::optional<StorePath>>>
    queryPathFromHashPart(const std::string & hashPart) override
    try {
        unsupported("queryPathFromHashPart");
    } catch (...) {
        return {result::current_exception()};
    }

    kj::Promise<Result<StorePath>> addToStoreRecursive(
        std::string_view name,
        const PreparedDump & source,
        HashType hashAlgo,
        RepairFlag repair) override
    try { throw Error("addToStoreRecursive"); } catch (...) { return {result::current_exception()}; }

    kj::Promise<Result<StorePath>> addToStoreFlat(
        std::string_view name,
        const Path & srcPath,
        HashType hashAlgo,
        RepairFlag repair) override
    try { throw Error("addToStoreFlat"); } catch (...) { return {result::current_exception()}; }

    kj::Promise<Result<StorePath>> addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair) override
    try { unsupported("addTextToStore"); } catch (...) { return {result::current_exception()}; }

private:

    WireFormatGenerator putBuildSettings(Connection & conn)
    {
        co_yield settings.maxSilentTime;
        co_yield settings.buildTimeout;
        co_yield settings.maxLogSize;
        co_yield 0; // buildRepeat hasn't worked for ages anyway
        co_yield 0;

        if (GET_PROTOCOL_MINOR(conn.remoteVersion) >= 7) {
            co_yield ((int) settings.keepFailed);
        }
    }

public:

    kj::Promise<Result<BuildResult>> buildDerivation(
        const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode
    ) override
    try {
        auto conn(TRY_AWAIT(connections->get()));

        co_return TRY_AWAIT(conn->sendCommand<BuildResult>(
            ServeProto::Command::BuildDerivation,
            printStorePath(drvPath),
            serializeDerivation(*this, drv),
            putBuildSettings(*conn)
        ));
    } catch (...) {
        co_return result::current_exception();
    }

    kj ::Promise<Result<void>> buildPaths(
        const std::vector<DerivedPath> & drvPaths,
        BuildMode buildMode,
        std::shared_ptr<Store> evalStore
    ) override
    try {
        if (evalStore && evalStore.get() != this)
            throw Error("building on an SSH store is incompatible with '--eval-store'");

        auto conn(TRY_AWAIT(connections->get()));

        Strings ss;
        for (auto & p : drvPaths) {
            auto sOrDrvPath = StorePathWithOutputs::tryFromDerivedPath(p);
            std::visit(overloaded {
                [&](const StorePathWithOutputs & s) {
                    ss.push_back(s.to_string(*this));
                },
                [&](const StorePath & drvPath) {
                    throw Error("wanted to fetch '%s' but the legacy ssh protocol doesn't support merely substituting drv files via the build paths command. It would build them instead. Try using ssh-ng://", printStorePath(drvPath));
                },
                [&](std::monostate) {
                    throw Error("wanted build derivation that is itself a build product, but the legacy ssh protocol doesn't support that. Try using ssh-ng://");
                },
            }, sOrDrvPath);
        }

        auto result = TRY_AWAIT(conn->sendCommand<BuildPathsResult>(
            ServeProto::Command::BuildPaths, ss, putBuildSettings(*conn)
        ));

        if (result.error) {
            throw *result.error;
        }

        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> ensurePath(const StorePath & path) override
    try { unsupported("ensurePath"); } catch (...) { return {result::current_exception()}; }

    virtual ref<FSAccessor> getFSAccessor() override
    { unsupported("getFSAccessor"); }

    /**
     * The default instance would schedule the work on the client side, but
     * for consistency with `buildPaths` and `buildDerivation` it should happen
     * on the remote side.
     *
     * We make this fail for now so we can add implement this properly later
     * without it being a breaking change.
     */
    kj::Promise<Result<void>> repairPath(const StorePath & path) override
    try { unsupported("repairPath"); } catch (...) { return {result::current_exception()}; }

    kj::Promise<Result<void>> computeFSClosure(const StorePathSet & paths,
        StorePathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false) override
    try {
        if (flipDirection || includeDerivers) {
            TRY_AWAIT(
                Store::computeFSClosure(paths, out, flipDirection, includeOutputs, includeDerivers)
            );
            co_return result::success();
        }

        auto conn(TRY_AWAIT(connections->get()));

        out.merge(TRY_AWAIT(conn->sendCommand<StorePathSet>(
            ServeProto::Command::QueryClosure, includeOutputs, ServeProto::write(*conn, paths)
        )));

        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<StorePathSet>> queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute) override
    try {
        auto conn(TRY_AWAIT(connections->get()));

        co_return TRY_AWAIT(conn->sendCommand<StorePathSet>(
            ServeProto::Command::QueryValidPaths,
            false, // lock
            maybeSubstitute,
            ServeProto::write(*conn, paths)
        ));
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> connect() override
    try {
        auto conn(TRY_AWAIT(connections->get()));
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<unsigned int>> getProtocol() override
    try {
        auto conn(TRY_AWAIT(connections->get()));
        co_return conn->remoteVersion;
    } catch (...) {
        co_return result::current_exception();
    }

    /**
     * The legacy ssh protocol doesn't support checking for trusted-user.
     * Try using ssh-ng:// instead if you want to know.
     */
    kj::Promise<Result<std::optional<TrustedFlag>>> isTrustedClient() override
    {
        return {result::success(std::nullopt)};
    }
};

void registerLegacySSHStore() {
    StoreImplementations::add<LegacySSHStore, LegacySSHStoreConfig>();
}

}
