#include "lix/libstore/ssh-store.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/remote-store.hh"
#include "lix/libstore/remote-store-connection.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libutil/pool.hh"
#include "lix/libstore/ssh.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/json.hh"
#include <memory>

namespace nix {

struct SSHStoreConfig : virtual RemoteStoreConfig, virtual CommonSSHStoreConfig
{
    using RemoteStoreConfig::RemoteStoreConfig;
    using CommonSSHStoreConfig::CommonSSHStoreConfig;

    const Setting<Path> remoteProgram{this, "nix-daemon", "remote-program",
        "Path to the `nix-daemon` executable on the remote machine."};

    const std::string name() override { return "Experimental SSH Store"; }

    std::string doc() override
    {
        return
          #include "ssh-store.md"
          ;
    }
};

class SSHStore final : public RemoteStore
{
    friend MustCallInit;

    SSHStoreConfig config_;

public:
    SSHStore(
        MustCallInit & w,
        kj::Badge<SSHStore>,
        const std::string & scheme,
        const std::string & host,
        SSHStoreConfig config
    )
        : Store(config)
        , RemoteStore(w, config)
        , config_(std::move(config))
        , host(host)
        , ssh(host, config_.port, config_.sshKey, config_.sshPublicHostKey, config_.compress)
    {
    }

    static kj::Promise<Result<std::optional<ref<Store>>>>
    open(const std::string & scheme, const Path & host, SSHStoreConfig config)
    try {
        MustCallInit init;
        auto store = make_ref<SSHStore>(init, kj::Badge<SSHStore>{}, scheme, host, std::move(config));
        TRY_AWAIT(init(store));
        co_return store;
    } catch (...) {
        co_return result::current_exception();
    }

    SSHStoreConfig & config() override
    {
        return config_;
    }
    const SSHStoreConfig & config() const override
    {
        return config_;
    }

    static inline const std::string scheme = "ssh-ng";

    std::string getUri() override
    {
        return scheme + "://" + host;
    }

    // FIXME extend daemon protocol, move implementation to RemoteStore
    kj::Promise<Result<std::optional<std::string>>> getBuildLogExact(const StorePath & path) override
    try {
        unsupported("getBuildLogExact");
    } catch (...) {
        return {result::current_exception()};
    }

protected:

    struct Connection : RemoteStore::Connection
    {
        // make sure this is destroyed last so the sshConn that feeds it dies first
        kj::Promise<void> logHandlerPromise{nullptr};
        std::unique_ptr<SSH::Connection> sshConn;

        ~Connection() noexcept = default;

        int getFD() const override
        {
            return sshConn->socket.get();
        }

        std::string connectErrorInfo() override
        {
            return chomp(drainFD(sshConn->stderrPipe.get(), false));
        }

        kj::Promise<void> logHandler(std::string storeUri)
        try {
            auto stderrPipe = std::move(sshConn->stderrPipe);
            auto reader = AIO().lowLevelProvider.wrapInputFd(stderrPipe.get());

            LogLineSplitter splitter;

            auto flushLine = [&](const std::string & line) { debug("ssh(%s): %s", storeUri, line); };

            auto buf = kj::heapArray<char>(4096);
            while (true) {
                const auto got = co_await reader->tryRead(buf.begin(), 1, buf.size());
                if (got == 0) {
                    break;
                }

                std::string_view data{buf.begin(), got};
                while (!data.empty()) {
                    if (auto line = splitter.feed(data)) {
                        flushLine(*line);
                    }
                }
            }

            if (auto line = splitter.finish(); !line.empty()) {
                flushLine(line);
            }
        } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
            logException("remote store error", e);
        } catch (...) {
            std::terminate();
        }
    };

    kj::Promise<Result<void>> init();

    std::string host;

    SSH ssh;

    kj::Promise<Result<void>> setOptions(RemoteStore::Connection & conn) override
    try {
        /* TODO Add a way to explicitly ask for some options to be
           forwarded. One option: A way to query the daemon for its
           settings, and then a series of params to SSHStore like
           forward-cores or forward-overridden-cores that only
           override the requested settings.
        */
        if (!conn.daemonNixVersion || !conn.daemonNixVersion->contains(sshNgExtendedOptionsFeature)) {
            co_return {result::success()};
        }

        std::map<std::string, std::string> overrides;

        auto override = [&](const auto & setting) {
            overrides.insert_or_assign(setting.name, setting.to_string());
        };

        // we only mirror what the legacy ssh protocol propagated for now
        override(settings.maxSilentTime);
        override(settings.buildTimeout);
        override(settings.keepFailed);

        StringSink command;

        // set unnamed values to something safe just in case the daemon lied to us about extended options
        // clang-format off
        command << WorkerProto::Op::SetOptions
            << false // keepFailed
            << false // keepGoing
            << false // tryFallback
            << lvlError // verbosity
            << 1 // maxBuildJobs
            << settings.maxSilentTime
            << WORKER_EXTENDED_OPTIONS_KEY1
            << false // verboseBuild
            << WORKER_EXTENDED_OPTIONS_KEY2
            << WORKER_EXTENDED_OPTIONS_KEY3
            << 1 // buildCores
            << false; // useSubstitutes
        // clang-format on

        command << 1 << WORKER_EXTENDED_OPTIONS_NAME << to_string(JSON{{"exact", overrides}});

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
};

kj::Promise<Result<void>> SSHStore::init()
try {
    auto conn = std::make_shared<Connection>();

    std::string command = config_.remoteProgram + " --stdio";
    if (config_.remoteStore.get() != "")
        command += " --store " + shellEscape(config_.remoteStore.get());

    conn->sshConn = ssh.startCommand(command);
    TRY_AWAIT(RemoteStore::initConnection(*conn));
    conn->logHandlerPromise = conn->logHandler(getUri());
    *(co_await connection.lock()) = conn;
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

void registerSSHStore() {
    StoreImplementations::add<SSHStore, SSHStoreConfig>({SSHStore::scheme});
}

}
