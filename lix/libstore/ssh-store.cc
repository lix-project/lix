#include "lix/libstore/ssh-store.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/remote-store.hh"
#include "lix/libstore/remote-store-connection.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libutil/pool.hh"
#include "lix/libstore/ssh.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/strings.hh"

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

struct SSHStoreConfigWithLog : SSHStoreConfig
{
    SSHStoreConfigWithLog(const Params & params)
        : StoreConfig(params)
        , RemoteStoreConfig(params)
        , CommonSSHStoreConfig(params)
        , SSHStoreConfig(params)
    {
    }

    // Hack for getting ssh errors into build-remote.
    // Intentionally not in `SSHStoreConfig` so that it doesn't appear in
    // the documentation
    const Setting<int> logFD{
        this, -1, "log-fd", "file descriptor to which SSH's stderr is connected"
    };
};

class SSHStore final : public RemoteStore
{
    SSHStoreConfigWithLog config_;

public:
    SSHStore(const std::string & scheme, const std::string & host, SSHStoreConfigWithLog config)
        : Store(config)
        , RemoteStore(config)
        , config_(std::move(config))
        , host(host)
        , ssh(host,
              config_.port,
              config_.sshKey,
              config_.sshPublicHostKey,
              config_.compress,
              config_.logFD)
    {
    }

    SSHStoreConfigWithLog & config() override
    {
        return config_;
    }
    const SSHStoreConfigWithLog & config() const override
    {
        return config_;
    }

    static std::set<std::string> uriSchemes() { return {"ssh-ng"}; }

    std::string getUri() override
    {
        return *uriSchemes().begin() + "://" + host;
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
        std::unique_ptr<SSH::Connection> sshConn;

        int getFD() const override
        {
            return sshConn->socket.get();
        }
    };

    ref<RemoteStore::Connection> openConnection() override;

    std::string host;

    SSH ssh;

    kj::Promise<Result<void>> setOptions(RemoteStore::Connection & conn) override
    {
        /* TODO Add a way to explicitly ask for some options to be
           forwarded. One option: A way to query the daemon for its
           settings, and then a series of params to SSHStore like
           forward-cores or forward-overridden-cores that only
           override the requested settings.
        */
        return {result::success()};
    };
};

ref<RemoteStore::Connection> SSHStore::openConnection()
{
    auto conn = make_ref<Connection>();

    std::string command = config_.remoteProgram + " --stdio";
    if (config_.remoteStore.get() != "")
        command += " --store " + shellEscape(config_.remoteStore.get());

    conn->sshConn = ssh.startCommand(command);
    return conn;
}

void registerSSHStore() {
    StoreImplementations::add<SSHStore, SSHStoreConfig>();
}

}
