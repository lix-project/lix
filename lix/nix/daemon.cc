///@file

#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libstore/remote-store.hh"
#include "lix/libstore/remote-store-connection.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async-collect.hh"
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
#include "lix/libutil/c-calls.hh"
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

#include <kj/async.h>
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

Config & daemonAuthorizationSettings = authorizationSettings;

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
 * Does the given user (specified by user name, primary group name and supplementary group names)
 * match the given user/group whitelist?
 *
 * If the list allows all users: Yes.
 *
 * If the username is in the set: Yes.
 *
 * If the user is in another group which is in the set: yes.
 * If the groups intersects in the set: Yes.
 *
 * Otherwise: No.
 */
static bool
matchUser(const std::string & user, const std::unordered_set<std::string> & groups, const Strings & users)
{
    if (find(users.begin(), users.end(), "*") != users.end())
        return true;

    if (find(users.begin(), users.end(), user) != users.end())
        return true;

    for (auto & i : users)
        if (i.substr(0, 1) == "@") {
            auto rest = i.substr(1);
            if (groups.contains(rest)) {
                return true;
            }
            struct group * gr = sys::getgrnam(rest);
            if (!gr) continue;
            if (matchUser(user, *gr)) return true;
        }

    return false;
}


struct PeerInfo
{
    std::optional<pid_t> pid;
    uid_t uid;
    gid_t gid;
    std::vector<gid_t> supplementaryGids;
};


/**
 * Get the identity of the caller, if possible.
 */
static PeerInfo getPeerInfo(int remote)
{
    std::vector<gid_t> supplementaryGids;

#if defined(SO_PEERCRED)
    ucred cred;
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) == -1)
        throw SysError("getting peer credentials");

#if defined(SO_PEERGROUPS)
    // NOTE(Raito):
    // Linux can go up to NGROUPS_MAX supplementary groups (65K).
    // It is safe to assume that most users will have a couple of supplementary
    // groups by default (here, my Linux user has ~7).
    // We allocate 128 because integers are tiny.
    supplementaryGids.resize(128);

    // Initially attempt to retrieve 128 groups.
    socklen_t nrSupplementaryGids = supplementaryGids.size();

    while (true) {

        if (getsockopt(remote, SOL_SOCKET, SO_PEERGROUPS, supplementaryGids.data(), &nrSupplementaryGids)
                == -1
            && errno != ERANGE)
        {
            throw SysError("getting peer groups");
        }

        // If the number of groups returned is less than the requested size, we are done.
        if (nrSupplementaryGids <= supplementaryGids.size()) {
            // We ensure the vector matches exactly the number of groups to avoid
            // letting the rest of the vector imply that the vector is full of `root` groups.
            supplementaryGids.resize(nrSupplementaryGids);
            break;
        }

        // Otherwise, the vector is too small. Resize and try again.
        nrSupplementaryGids *= 2;

        // We ensure the vector is big enough in response to our latest known allocation requirement.
        supplementaryGids.resize(nrSupplementaryGids);
    }
#endif
    PeerInfo peer = {cred.pid, cred.uid, cred.gid, supplementaryGids};

#elif defined(LOCAL_PEERCRED)

#if !defined(SOL_LOCAL)
#define SOL_LOCAL 0
#endif

    xucred cred;
    socklen_t credLen = sizeof(cred);
    if (getsockopt(remote, SOL_LOCAL, LOCAL_PEERCRED, &cred, &credLen) == -1)
        throw SysError("getting peer credentials");

    size_t nrGroups = cred.cr_ngroups;
    std::copy(cred.cr_groups, cred.cr_groups + nrGroups, std::back_inserter(supplementaryGids));

    PeerInfo peer = {std::nullopt, cred.cr_uid, cred.cr_gid, supplementaryGids};

#if defined(LOCAL_PEERPID)
    socklen_t pidLen = sizeof(peer.pid);
    pid_t peerPid;
    if (!getsockopt(remote, SOL_LOCAL, LOCAL_PEERPID, &peerPid, &pidLen)) {
        peer.pid = peerPid;
    }

#endif

#else

#error \
    "Your platform does not provide a mean (SO_PEERCRED or LOCAL_PEERCRED) to receive the user credentials when a connection to a socket is made. Please provide one."

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

    struct passwd * pw = getpwuid(peer.uid);
    std::string user = pw ? pw->pw_name : std::to_string(peer.uid);

    auto assertHealthyGroup = [&](const std::string & group) {
        if (group == settings.buildUsersGroup) {
            throw Error(
                "the user '%1%' is not allowed to connect to the Nix daemon as its group is '%2%', "
                "which is the group of users running the sandboxed builds.",
                user,
                group
            );
        }
    };

    std::unordered_set<std::string> groups;

    auto insertGroup = [&](gid_t gid) {
        auto gr = getgrgid(gid);
        std::string group = gr ? gr->gr_name : std::to_string(gid);
        assertHealthyGroup(group);
        groups.insert(group);
    };

    // This ensures that the `groups` set is always non-empty with the primary group name.
    insertGroup(peer.gid);

    for (const gid_t suppGid : peer.supplementaryGids) {
        insertGroup(suppGid);
    }

    const Strings & trustedUsers = authorizationSettings.trustedUsers;
    const Strings & allowedUsers = authorizationSettings.allowedUsers;

    if (matchUser(user, groups, trustedUsers)) {
        trusted = Trusted;
    }

    if (!trusted && !matchUser(user, groups, allowedUsers)) {
        throw Error("user '%1%' is not allowed to connect to the Nix daemon", user);
    }

    return { trusted, std::move(user) };
}

static kj::Promise<Result<void>> daemonLoopForSocket(
    const Path & self,
    const Settings::DaemonSocketPath & socket,
    AutoCloseFD & fdSocket,
    std::optional<TrustedFlag> forceTrustClientOpt
)
try {
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
            printInfo("accepted connection from %1%", peer.pid ? fmt("pid %1%", *peer.pid) : "unknown peer");

            // Fork a child to handle the connection. make sure it's called with
            // argv0 `nix-daemon` so we don't try to run `nix --for` when called
            // from more modern scripts that assume nix-command being available.
            RunOptions options{
                .program = self,
                .argv0 = "nix-daemon",
                .args =
                    {
                        "--for",
                        peer.pid ? fmt("%1%", *peer.pid) : "unknown",
                        "--log-level",
                        fmt("%1%", int(verbosity)),
                    },
                .redirections = {{.dup = SUBDAEMON_CONNECTION_FD, .from = remote.get()}}
            };
            if (forceTrustClientOpt) {
                options.args.push_back(
                    *forceTrustClientOpt ? "--force-trusted" : "--force-untrusted"
                );
            }
            auto [pid, _stdout] = runProgram2(options).release();
            pid.release();
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
    if (chdir("/") == -1) {
        throw SysError("cannot change current directory");
    }

    const auto self = [] {
        auto tmp = getSelfExe();
        if (!tmp) {
            throw Error("can't locate the daemon binary!");
        }
        return *tmp;
    }();

    std::list<std::pair<Settings::DaemonSocketPath, AutoCloseFD>> sockets;
    for (auto & socket : settings.nixDaemonSockets()) {
        createDirs(dirOf(socket.path));
        sockets.emplace_back(socket, createUnixDomainSocket(socket.path, 0666));
    }

    //  Get rid of children automatically; don't let them become zombies.
    setSigChldAction(true);

    TRY_AWAIT(asyncSpread(sockets, [&](auto & socket) {
        return daemonLoopForSocket(self, socket.first, socket.second, forceTrustClientOpt);
    }));

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

static void
daemonInstance(AsyncIoRoot & aio, std::optional<TrustedFlag> forceTrustClientOpt, char * peerPidArg)
{
    //  Handle socket-based activation by systemd.
    const auto [launchedByManager, connectionFd] = []() -> std::pair<bool, int> {
        auto listenFds = getEnv("LISTEN_FDS");
        if (listenFds) {
            if (getEnv("LISTEN_PID") != std::to_string(getpid()) || listenFds != "1") {
                throw Error("unexpected systemd environment variables");
            }
            closeOnExec(SD_LISTEN_FDS_START);
            // these unsets are not critical, we never did this for accept=no sockets either
            (void) sys::unsetenv("LISTEN_FDS");
            (void) sys::unsetenv("LISTEN_PID");
            (void) sys::unsetenv("LISTEN_FDNAMES");
            return {true, SD_LISTEN_FDS_START};
        } else {
            return {false, SUBDAEMON_CONNECTION_FD};
        }
    }();

    PeerInfo peer = getPeerInfo(connectionFd);
    TrustedFlag trusted;
    std::string user;

    // replace peerPidArg contents with the peer pid if possible. the forking daemon does
    // this as a debugging aid and it is easy enough to do it here also, so we just do it
    if (peerPidArg && peer.pid) {
        auto pidForArgv = std::to_string(*peer.pid);
        if (pidForArgv.size() < strlen(peerPidArg)) {
            memset(peerPidArg, ' ', strlen(peerPidArg));
            peerPidArg[0] = '\0';
            memcpy(peerPidArg + 1, pidForArgv.c_str(), pidForArgv.size());
        }
    }

    if (forceTrustClientOpt) {
        trusted = *forceTrustClientOpt;
    } else {
        std::tie(trusted, user) = authPeer(peer);
    }

    printInfo(
        "%1% is %2% (%3%%4%)",
        Uncolored(peer.pid ? fmt("remote pid %s", *peer.pid) : "remote with unknown pid"),
        peer.uid ? fmt("user %s", user) : "unknown user",
        trusted ? "trusted" : "untrusted",
        forceTrustClientOpt ? " by override" : ""
    );

    //  Background the daemon.
    if (!launchedByManager && setsid() == -1) {
        throw SysError("creating a new session");
    }

    auto store = aio.blockOn(openUncachedStore(AllowDaemon::Disallow));
    if (auto local = dynamic_cast<LocalStore *>(&*store); local) {
        local->associateWithCredentials(peer.uid, peer.gid);
    }

    //  Handle the connection.
    FdSource from(connectionFd);
    FdSink to(connectionFd);
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

static int
main_nix_daemon(AsyncIoRoot & aio, std::string programName, Strings argv, std::span<char *> rawArgv)
{
    {
        auto stdio = false;
        std::optional<TrustedFlag> isTrustedOpt = std::nullopt;
        bool isInstance = false;
        char * peerPidArg = nullptr;
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
            } else if (*arg == "--for-socket-activation") {
                isInstance = true;
                // HACK: too many copies and rewrites happen by the time we get here to
                // be able to calculate a rawArgv offset. instead we will search for an
                // exact match and blindly assume that it's the one we want to rewrite.
                for (auto [i, rawArg] : enumerate(rawArgv)) {
                    if (rawArg == *arg) {
                        peerPidArg = rawArg + strlen("--for");
                        break;
                    }
                }
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
            daemonInstance(aio, isTrustedOpt, peerPidArg);
        } else {
            runDaemon(aio, stdio, isTrustedOpt);
        }

        return 0;
    }
}

void registerLegacyNixDaemon() {
    LegacyCommandRegistry::addWithRaw("nix-daemon", main_nix_daemon);
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
