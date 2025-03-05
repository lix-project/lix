#include "lix/libstore/legacy-ssh-store.hh"
#include "lix/libutil/archive.hh"
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
#include "lix/libutil/strings.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libutil/config-impl.hh"
#include "lix/libutil/abstract-setting-to-json.hh"

namespace nix {

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
        std::unique_ptr<SSHMaster::Connection> sshConn;
        FdSink to;
        FdSource from;
        ServeProto::Version remoteVersion;
        bool good = true;

        /**
         * Coercion to `ServeProto::ReadConn`. This makes it easy to use the
         * factored out serve protocol searlizers with a
         * `LegacySSHStore::Connection`.
         *
         * The serve protocol connection types are unidirectional, unlike
         * this type.
         */
        operator ServeProto::ReadConn ()
        {
            return ServeProto::ReadConn {
                .from = from,
                .version = remoteVersion,
            };
        }

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
                .version = remoteVersion,
            };
        }
    };

    std::string host;

    ref<Pool<Connection>> connections;

    SSHMaster master;

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
        , master(
            host,
            config_.port,
            config_.sshKey,
            config_.sshPublicHostKey,
            // Use SSH master only if using more than 1 connection.
            connections->capacity() > 1,
            config_.compress,
            config_.logFD)
    {
    }

    ref<Connection> openConnection()
    {
        auto conn = make_ref<Connection>();
        conn->sshConn = master.startCommand(
            fmt("%s --serve --write", config_.remoteProgram)
            + (config_.remoteStore.get() == ""
                   ? ""
                   : " --store " + shellEscape(config_.remoteStore.get()))
        );
        conn->to = FdSink(conn->sshConn->in.get());
        conn->from = FdSource(conn->sshConn->out.get());

        try {
            conn->to << SERVE_MAGIC_1 << SERVE_PROTOCOL_VERSION;
            conn->to.flush();

            uint64_t magic = readLongLong(conn->from);
            if (magic != SERVE_MAGIC_2)
                throw Error("'nix-store --serve' protocol mismatch from '%s'", host);
            conn->remoteVersion = readInt(conn->from);
            if (GET_PROTOCOL_MAJOR(conn->remoteVersion) != 0x200)
                throw Error("unsupported 'nix-store --serve' protocol version on '%s'", host);

        } catch (EndOfFile & e) {
            throw Error("cannot connect to '%1%'", host);
        }

        return conn;
    };

    std::string getUri() override
    {
        return *uriSchemes().begin() + "://" + host;
    }

    std::shared_ptr<const ValidPathInfo> queryPathInfoUncached(const StorePath & path) override
    {
        auto conn(connections->get());

        /* No longer support missing NAR hash */
        assert(GET_PROTOCOL_MINOR(conn->remoteVersion) >= 4);

        debug("querying remote host '%s' for info on '%s'", host, printStorePath(path));

        conn->to << ServeProto::Command::QueryPathInfos << PathSet{printStorePath(path)};
        conn->to.flush();

        auto p = readString(conn->from);
        if (p.empty()) return nullptr;
        auto path2 = parseStorePath(p);
        assert(path == path2);
        auto info = std::make_shared<ValidPathInfo>(
            path,
            ServeProto::Serialise<UnkeyedValidPathInfo>::read(*this, *conn));

        if (info->narHash == Hash::dummy)
            throw Error("NAR hash is now mandatory");

        auto s = readString(conn->from);
        assert(s == "");

        return info;
    }

    kj::Promise<Result<void>> addToStore(const ValidPathInfo & info, AsyncInputStream & source,
        RepairFlag repair, CheckSigsFlag checkSigs) override
    try {
        debug("adding path '%s' to remote host '%s'", printStorePath(info.path), host);

        auto conn(connections->get());

        if (GET_PROTOCOL_MINOR(conn->remoteVersion) >= 5) {

            conn->to
                << ServeProto::Command::AddToStoreNar
                << printStorePath(info.path)
                << (info.deriver ? printStorePath(*info.deriver) : "")
                << info.narHash.to_string(Base::Base16, false);
            conn->to << ServeProto::write(*this, *conn, info.references);
            conn->to
                << info.registrationTime
                << info.narSize
                << info.ultimate
                << info.sigs
                << renderContentAddress(info.ca);
            try {
                TRY_AWAIT(copyNAR(source)->drainInto(conn->to));
            } catch (...) {
                conn->good = false;
                throw;
            }
            conn->to.flush();

        } else {

            conn->to
                << ServeProto::Command::ImportPaths
                << 1;
            try {
                TRY_AWAIT(copyNAR(source)->drainInto(conn->to));
            } catch (...) {
                conn->good = false;
                throw;
            }
            conn->to
                << exportMagic
                << printStorePath(info.path);
            conn->to << ServeProto::write(*this, *conn, info.references);
            conn->to
                << (info.deriver ? printStorePath(*info.deriver) : "")
                << 0
                << 0;
            conn->to.flush();

        }

        if (readInt(conn->from) != 1)
            throw Error("failed to add path '%s' to remote host '%s'", printStorePath(info.path), host);
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    box_ptr<Source> narFromPath(const StorePath & path) override
    {
        auto conn(connections->get());

        conn->to << ServeProto::Command::DumpStorePath << printStorePath(path);
        conn->to.flush();
        return make_box_ptr<GeneratorSource>([] (auto conn) -> WireFormatGenerator {
            co_yield copyNAR(conn->from);
        }(std::move(conn)));
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

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

    void putBuildSettings(Connection & conn)
    {
        conn.to
            << settings.maxSilentTime
            << settings.buildTimeout;
        if (GET_PROTOCOL_MINOR(conn.remoteVersion) >= 2)
            conn.to
                << settings.maxLogSize;
        if (GET_PROTOCOL_MINOR(conn.remoteVersion) >= 3)
            conn.to
                << 0 // buildRepeat hasn't worked for ages anyway
                << 0;

        if (GET_PROTOCOL_MINOR(conn.remoteVersion) >= 7) {
            conn.to << ((int) settings.keepFailed);
        }
    }

public:

    kj::Promise<Result<BuildResult>> buildDerivation(
        const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode
    ) override
    try {
        auto conn(connections->get());

        conn->to
            << ServeProto::Command::BuildDerivation
            << printStorePath(drvPath);
        writeDerivation(conn->to, *this, drv);

        putBuildSettings(*conn);

        conn->to.flush();

        return {ServeProto::Serialise<BuildResult>::read(*this, *conn)};
    } catch (...) {
        return {result::current_exception()};
    }

    kj ::Promise<Result<void>> buildPaths(
        const std::vector<DerivedPath> & drvPaths,
        BuildMode buildMode,
        std::shared_ptr<Store> evalStore
    ) override
    try {
        if (evalStore && evalStore.get() != this)
            throw Error("building on an SSH store is incompatible with '--eval-store'");

        auto conn(connections->get());

        conn->to << ServeProto::Command::BuildPaths;
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
        conn->to << ss;

        putBuildSettings(*conn);

        conn->to.flush();

        BuildResult result;
        result.status = (BuildResult::Status) readInt(conn->from);

        if (!result.success()) {
            conn->from >> result.errorMsg;
            throw Error(result.status, result.errorMsg);
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

        auto conn(connections->get());

        conn->to
            << ServeProto::Command::QueryClosure
            << includeOutputs;
        conn->to << ServeProto::write(*this, *conn, paths);
        conn->to.flush();

        for (auto & i : ServeProto::Serialise<StorePathSet>::read(*this, *conn))
            out.insert(i);
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<StorePathSet>> queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute) override
    try {
        auto conn(connections->get());

        conn->to
            << ServeProto::Command::QueryValidPaths
            << false // lock
            << maybeSubstitute;
        conn->to << ServeProto::write(*this, *conn, paths);
        conn->to.flush();

        co_return ServeProto::Serialise<StorePathSet>::read(*this, *conn);
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> connect() override
    try {
        auto conn(connections->get());
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<unsigned int>> getProtocol() override
    try {
        auto conn(connections->get());
        co_return conn->remoteVersion;
    } catch (...) {
        co_return result::current_exception();
    }

    /**
     * The legacy ssh protocol doesn't support checking for trusted-user.
     * Try using ssh-ng:// instead if you want to know.
     */
    std::optional<TrustedFlag> isTrustedClient() override
    {
        return std::nullopt;
    }

    std::shared_ptr<const Realisation> queryRealisationUncached(const DrvOutput &) override
    // TODO: Implement
    { unsupported("queryRealisation"); }
};

void registerLegacySSHStore() {
    StoreImplementations::add<LegacySSHStore, LegacySSHStoreConfig>();
}

}
