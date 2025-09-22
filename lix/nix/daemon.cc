///@file

#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libstore/remote-store.hh"
#include "lix/libstore/remote-store-connection.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/current-process.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/archive.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libutil/finally.hh"
#include "lix/libcmd/legacy.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/daemon.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/unix-domain-socket.hh"
#include "daemon.hh"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>

#include <string>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>

#if __APPLE__ || __FreeBSD__
#include <sys/ucred.h>
#endif
#if __APPLE__
#include <membership.h>
#endif

static constexpr int SUBDAEMON_CONNECTION_FD = 0;
static constexpr int SUBDAEMON_SETTINGS_FD = 3;

namespace nix {

using namespace nix::daemon;

/**
 * Settings related to authenticating clients for the Nix daemon.
 *
 * For pipes we have little good information about the client side, but
 * for Unix domain sockets we do. So currently these options implemented
 * mandatory access control based on user names and group names (looked
 * up and translated to UID/GIDs in the CLI process that runs the code
 * in this file).
 *
 * No code outside of this file knows about these settings (this is not
 * exposed in a header); all authentication and authorization happens in
 * `daemon.cc`.
 */
struct AuthorizationSettings : Config {
    #include "daemon-settings.gen.inc"
};

AuthorizationSettings authorizationSettings;

static GlobalConfig::Register rSettings(&authorizationSettings);

static void sigChldHandler(int sigNo)
{
    // Ensure we don't modify errno of whatever we've interrupted
    auto saved_errno = errno;
    //  Reap all dead children.
    while (waitpid(-1, 0, WNOHANG) > 0) ;
    errno = saved_errno;
}


static void setSigChldAction(bool autoReap)
{
    struct sigaction act, oact;
    act.sa_handler = autoReap ? sigChldHandler : SIG_DFL;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGCHLD, &act, &oact))
        throw SysError("setting SIGCHLD handler");
}

/**
 * @return Is the given user a member of this group?
 *
 * @param user User specified by username.
 *
 * @param group Group the user might be a member of.
 */
static bool matchUser(const std::string & user, const struct group & gr)
{
    for (char * * mem = gr.gr_mem; *mem; mem++)
        if (user == *mem) return true;
#if __APPLE__
    // FIXME: we should probably pipe the uid through these functions
    // instead of converting the username back into the uid
    if (auto pw = getpwnam(user.c_str())) {
        uuid_t uuid, gruuid;
        if (!mbr_uid_to_uuid(pw->pw_uid, uuid) && !mbr_gid_to_uuid(gr.gr_gid, gruuid)) {
            int ismember = 0;
            if (!mbr_check_membership(uuid, gruuid, &ismember)) {
                return !!ismember;
            }
        }
    }
#endif
    return false;
}


/**
 * Does the given user (specified by user name and primary group name)
 * match the given user/group whitelist?
 *
 * If the list allows all users: Yes.
 *
 * If the username is in the set: Yes.
 *
 * If the groupname is in the set: Yes.
 *
 * If the user is in another group which is in the set: yes.
 *
 * Otherwise: No.
 */
static bool matchUser(const std::string & user, const std::string & group, const Strings & users)
{
    if (find(users.begin(), users.end(), "*") != users.end())
        return true;

    if (find(users.begin(), users.end(), user) != users.end())
        return true;

    for (auto & i : users)
        if (i.substr(0, 1) == "@") {
            if (group == i.substr(1)) return true;
            struct group * gr = getgrnam(i.c_str() + 1);
            if (!gr) continue;
            if (matchUser(user, *gr)) return true;
        }

    return false;
}


struct PeerInfo
{
    bool pidKnown;
    pid_t pid;
    bool uidKnown;
    uid_t uid;
    bool gidKnown;
    gid_t gid;
};


/**
 * Get the identity of the caller, if possible.
 */
static PeerInfo getPeerInfo(int remote)
{
    PeerInfo peer = { false, 0, false, 0, false, 0 };

#if defined(SO_PEERCRED)

    ucred cred;
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) == -1)
        throw SysError("getting peer credentials");
    peer = { true, cred.pid, true, cred.uid, true, cred.gid };

#elif defined(LOCAL_PEERCRED)

#if !defined(SOL_LOCAL)
#define SOL_LOCAL 0
#endif

    xucred cred;
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_LOCAL, LOCAL_PEERCRED, &cred, &credLen) == -1)
        throw SysError("getting peer credentials");
    peer = { false, 0, true, cred.cr_uid, true, cred.cr_gid };

#if defined(LOCAL_PEERPID)
    socklen_t pidLen = sizeof(peer.pid);
    if (!getsockopt(remote, SOL_LOCAL, LOCAL_PEERPID, &peer.pid, &pidLen))
        peer.pidKnown = true;
#endif

#endif

    return peer;
}


#define SD_LISTEN_FDS_START 3


/**
 * Open a store without a path info cache.
 */
static kj::Promise<Result<ref<Store>>> openUncachedStore(AllowDaemon allowDaemon = AllowDaemon::Allow)
try {
    StoreConfig::Params params; // FIXME: get params from somewhere
    // Disable caching since the client already does that.
    params["path-info-cache-size"] = "0";
    co_return TRY_AWAIT(openStore(settings.storeUri, params, allowDaemon));
} catch (...) {
    co_return result::current_exception();
}

/**
 * Authenticate a potential client
 *
 * @param peer Information about other end of the connection, the client which
 * wants to communicate with us.
 *
 * @return A pair of a `TrustedFlag`, whether the potential client is trusted,
 * and the name of the user (useful for printing messages).
 *
 * If the potential client is not allowed to talk to us, we throw an `Error`.
 */
static std::pair<TrustedFlag, std::string> authPeer(const PeerInfo & peer)
{
    TrustedFlag trusted = NotTrusted;

    struct passwd * pw = peer.uidKnown ? getpwuid(peer.uid) : 0;
    std::string user = pw ? pw->pw_name : std::to_string(peer.uid);

    struct group * gr = peer.gidKnown ? getgrgid(peer.gid) : 0;
    std::string group = gr ? gr->gr_name : std::to_string(peer.gid);

    const Strings & trustedUsers = authorizationSettings.trustedUsers;
    const Strings & allowedUsers = authorizationSettings.allowedUsers;

    if (matchUser(user, group, trustedUsers))
        trusted = Trusted;

    if ((!trusted && !matchUser(user, group, allowedUsers)) || group == settings.buildUsersGroup)
        throw Error("user '%1%' is not allowed to connect to the Nix daemon", user);

    return { trusted, std::move(user) };
}


/**
 * Run a server. The loop opens a socket and accepts new connections from that
 * socket.
 *
 * @param forceTrustClientOpt If present, force trusting or not trusted
 * the client. Otherwise, decide based on the authentication settings
 * and user credentials (from the unix domain socket).
 */
static kj::Promise<Result<void>> daemonLoop(std::optional<TrustedFlag> forceTrustClientOpt)
try {
    if (chdir("/") == -1)
        throw SysError("cannot change current directory");

    AutoCloseFD fdSocket;

    //  Handle socket-based activation by systemd.
    auto listenFds = getEnv("LISTEN_FDS");
    if (listenFds) {
        if (getEnv("LISTEN_PID") != std::to_string(getpid()) || listenFds != "1")
            throw Error("unexpected systemd environment variables");
        fdSocket = AutoCloseFD{SD_LISTEN_FDS_START};
        closeOnExec(fdSocket.get());
    }

    //  Otherwise, create and bind to a Unix domain socket.
    else {
        createDirs(dirOf(settings.nixDaemonSocketFile));
        fdSocket = createUnixDomainSocket(settings.nixDaemonSocketFile, 0666);
    }

    //  Get rid of children automatically; don't let them become zombies.
    setSigChldAction(true);

    const auto self = [] {
        auto tmp = getSelfExe();
        if (!tmp) {
            throw Error("can't locate the daemon binary!");
        }
        return *tmp;
    }();

    makeNonBlocking(fdSocket.get());
    auto observer = kj::UnixEventPort::FdObserver{
        AIO().unixEventPort, fdSocket.get(), kj::UnixEventPort::FdObserver::OBSERVE_READ
    };

    //  Loop accepting connections.
    while (1) {

        try {
            //  Accept a connection.
            struct sockaddr_un remoteAddr;
            socklen_t remoteAddrLen = sizeof(remoteAddr);

            AutoCloseFD remote{accept(
                fdSocket.get(), reinterpret_cast<struct sockaddr *>(&remoteAddr), &remoteAddrLen
            )};
            if (!remote) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    co_await observer.whenBecomesReadable();
                    continue;
                } else if (errno == EINTR) {
                    continue;
                }
                throw SysError("accepting connection");
            }

            // On macOS, accepted sockets inherit the non-blocking flag from the server socket, so
            // explicitly make it blocking.
            makeBlocking(remote.get());

            closeOnExec(remote.get());

            PeerInfo peer = getPeerInfo(remote.get());
            printInfo(
                "accepted connection from %1%",
                peer.pidKnown ? fmt("pid %1%", peer.pid) : "unknown peer"
            );

            Pipe settings;
            settings.create();

            // Fork a child to handle the connection. make sure it's called with
            // argv0 `nix-daemon` so we don't try to run `nix --for` when called
            // from more modern scripts that assume nix-command being available.
            RunOptions options{
                .program = self,
                .argv0 = "nix-daemon",
                .args =
                    {
                        "--for",
                        peer.pidKnown ? fmt("%1%", peer.pid) : "unknown",
                        "--log-level",
                        fmt("%1%", int(verbosity)),
                    },
                .dieWithParent = false,
                .redirections =
                    {
                        {.dup = SUBDAEMON_CONNECTION_FD, .from = remote.get()},
                        {.dup = SUBDAEMON_SETTINGS_FD, .from = settings.readSide.get()},
                    }
            };
            if (forceTrustClientOpt) {
                options.args.push_back(
                    *forceTrustClientOpt ? "--force-trusted" : "--force-untrusted"
                );
            }
            runProgram2(options).release();

            FdSink sink(settings.writeSide.get());
            std::map<std::string, Config::SettingInfo> overriddenSettings;
            globalConfig.getSettings(overriddenSettings, true);
            for (auto & setting : overriddenSettings) {
                sink << 1 << setting.first << setting.second.value;
            }
            sink << 0;
            sink.flush();
        } catch (Error & error) {
            auto ei = error.info();
            // FIXME: add to trace?
            ei.msg = HintFmt("error processing connection: %1%", ei.msg.str());
            logError(ei);
        }
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

static void daemonInstance(AsyncIoRoot & aio, std::optional<TrustedFlag> forceTrustClientOpt)
{
    PeerInfo peer = getPeerInfo(SUBDAEMON_CONNECTION_FD);
    TrustedFlag trusted;
    std::string user;

    if (forceTrustClientOpt) {
        trusted = *forceTrustClientOpt;
    } else {
        std::tie(trusted, user) = authPeer(peer);
    }

    printInfo(
        "%1% is %2% (%3%%4%)",
        Uncolored(peer.pidKnown ? fmt("remote pid %s", peer.pid) : "remote with unknown pid"),
        peer.uidKnown ? fmt("user %s", user) : "unknown user",
        trusted ? "trusted" : "untrusted",
        forceTrustClientOpt ? " by override" : ""
    );

    {
        FdSource source(SUBDAEMON_SETTINGS_FD);

        /* Read the parent's settings. */
        while (readNum<unsigned>(source)) {
            auto name = readString(source);
            auto value = readString(source);
            settings.set(name, value);
        }

        if (close(SUBDAEMON_SETTINGS_FD) < 0) {
            throw SysError("preparing subdaemon connection");
        }
    }

    //  Background the daemon.
    if (setsid() == -1) {
        throw SysError("creating a new session");
    }

    //  Restore normal handling of SIGCHLD.
    setSigChldAction(false);

    auto store = aio.blockOn(openUncachedStore(AllowDaemon::Disallow));
    if (auto local = dynamic_cast<LocalStore *>(&*store); local && peer.uidKnown && peer.gidKnown) {
        local->associateWithCredentials(peer.uid, peer.gid);
    }

    //  Handle the connection.
    FdSource from(SUBDAEMON_CONNECTION_FD);
    FdSink to(SUBDAEMON_CONNECTION_FD);
    processConnection(aio, store, from, to, trusted);
}

/**
 * Forward a standard IO connection to the given remote store.
 *
 * We just act as a middleman blindly ferry output between the standard
 * input/output and the remote store connection, not processing anything.
 *
 * Loops until standard input disconnects, or an error is encountered.
 */
static void forwardStdioConnection(AsyncIoRoot & aio, RemoteStore & store)
{
    auto conn = store.openConnectionWrapper();
    auto connSocket = AIO().lowLevelProvider.wrapSocketFd(conn->getFD());
    auto asyncStdin = AIO().lowLevelProvider.wrapInputFd(STDIN_FILENO);
    auto asyncStdout = AIO().lowLevelProvider.wrapOutputFd(STDOUT_FILENO);

    aio.blockOn(connSocket->pumpTo(*asyncStdout)
                    .then([](auto) -> Result<void> {
                        return {
                            std::make_exception_ptr(EndOfFile("unexpected EOF from daemon socket"))
                        };
                    })
                    .exclusiveJoin(asyncStdin->pumpTo(*connSocket).then([](auto) -> Result<void> {
                        return result::success();
                    })));
}

/**
 * Process a client connecting to us via standard input/output
 *
 * Unlike `forwardStdioConnection()` we do process commands ourselves in
 * this case, not delegating to another daemon.
 *
 * @param trustClient Whether to trust the client. Forwarded directly to
 * `processConnection()`.
 */
static void
processStdioConnection(AsyncIoRoot & aio, ref<Store> store, TrustedFlag trustClient)
{
    FdSource from(STDIN_FILENO);
    FdSink to(STDOUT_FILENO);
    processConnection(aio, store, from, to, trustClient);
}

/**
 * Entry point shared between the new CLI `nix daemon` and old CLI
 * `nix-daemon`.
 *
 * @param forceTrustClientOpt See `daemonLoop()` and the parameter with
 * the same name over there for details.
 */
static void
runDaemon(AsyncIoRoot & aio, bool stdio, std::optional<TrustedFlag> forceTrustClientOpt)
{
    if (stdio) {
        auto store = aio.blockOn(openUncachedStore());

        // If --force-untrusted is passed, we cannot forward the connection and
        // must process it ourselves (before delegating to the next store) to
        // force untrusting the client.
        if (auto remoteStore = store.try_cast_shared<RemoteStore>(); remoteStore && (!forceTrustClientOpt || *forceTrustClientOpt != NotTrusted))
            forwardStdioConnection(aio, *remoteStore);
        else
            // `Trusted` is passed in the auto (no override case) because we
            // cannot see who is on the other side of a plain pipe. Limiting
            // access to those is explicitly not `nix-daemon`'s responsibility.
            processStdioConnection(aio, store, forceTrustClientOpt.value_or(Trusted));
    } else {
        try {
            aio.blockOn(makeInterruptible(daemonLoop(forceTrustClientOpt)));
        } catch (Interrupted &) {
        }
    }
}

static int main_nix_daemon(AsyncIoRoot & aio, std::string programName, Strings argv)
{
    {
        auto stdio = false;
        std::optional<TrustedFlag> isTrustedOpt = std::nullopt;
        bool isInstance = false;
        Verbosity subdaemonLogLevel = lvlInfo;

        LegacyArgs(aio, programName, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--daemon")
                ; //  ignored for backwards compatibility
            else if (*arg == "--help")
                showManPage("nix-daemon");
            else if (*arg == "--version")
                printVersion("nix-daemon");
            else if (*arg == "--stdio")
                stdio = true;
            else if (*arg == "--force-trusted") {
                experimentalFeatureSettings.require(Xp::DaemonTrustOverride);
                isTrustedOpt = Trusted;
            } else if (*arg == "--force-untrusted") {
                experimentalFeatureSettings.require(Xp::DaemonTrustOverride);
                isTrustedOpt = NotTrusted;
            } else if (*arg == "--default-trust") {
                experimentalFeatureSettings.require(Xp::DaemonTrustOverride);
                isTrustedOpt = std::nullopt;
            } else if (*arg == "--for") {
                isInstance = true;
                getArg(*arg, arg, end);
            } else if (*arg == "--log-level") {
                if (auto level = string2Int<int>(getArg(*arg, arg, end)); level) {
                    subdaemonLogLevel = static_cast<Verbosity>(std::min<int>(lvlVomit, *level));
                } else {
                    throw UsageError("--log-level expects an integer in the range [0..7]");
                }
            } else {
                return false;
            }
            return true;
        }).parseCmdline(argv);

        if (isInstance) {
            verbosity = Verbosity(std::min<uint64_t>(subdaemonLogLevel, lvlVomit));
            daemonInstance(aio, isTrustedOpt);
        } else {
            runDaemon(aio, stdio, isTrustedOpt);
        }

        return 0;
    }
}

void registerLegacyNixDaemon() {
    LegacyCommandRegistry::add("nix-daemon", main_nix_daemon);
}

struct CmdDaemon : StoreCommand
{
    bool stdio = false;
    std::optional<TrustedFlag> isTrustedOpt = std::nullopt;

    CmdDaemon()
    {
        addFlag({
            .longName = "stdio",
            .description = "Attach to standard I/O, instead of trying to bind to a UNIX socket.",
            .handler = {&stdio, true},
        });

        addFlag({
            .longName = "force-trusted",
            .description = "Force the daemon to trust connecting clients.",
            .handler = {[&]() {
                isTrustedOpt = Trusted;
            }},
            .experimentalFeature = Xp::DaemonTrustOverride,
        });

        addFlag({
            .longName = "force-untrusted",
            .description = "Force the daemon to not trust connecting clients. The connection will be processed by the receiving daemon before forwarding commands.",
            .handler = {[&]() {
                isTrustedOpt = NotTrusted;
            }},
            .experimentalFeature = Xp::DaemonTrustOverride,
        });

        addFlag({
            .longName = "default-trust",
            .description = "Use Nix's default trust.",
            .handler = {[&]() {
                isTrustedOpt = std::nullopt;
            }},
            .experimentalFeature = Xp::DaemonTrustOverride,
        });
    }

    std::string description() override
    {
        return "daemon to perform store operations on behalf of non-root clients";
    }

    Category category() override { return catUtility; }

    std::string doc() override
    {
        return
          #include "daemon.md"
          ;
    }

    void run(ref<Store> store) override
    {
        runDaemon(aio(), stdio, isTrustedOpt);
    }
};

void registerNixDaemon()
{
    registerCommand2<CmdDaemon>({"daemon"});
}

}
