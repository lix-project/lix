#include "lix/libutil/current-process.hh"
#include "lix/libutil/environment-variables.hh"
#include "lix/libstore/ssh.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/strings.hh"
#include "lix/libstore/temporary-dir.hh"
#include <sys/socket.h>
#include <unistd.h>

namespace nix {

SSH::SSH(const std::string & host, const std::optional<uint16_t> port, const std::string & keyFile, const std::string & sshPublicHostKey, bool compress, int logFD)
    : host(host)
    , port(port)
    , fakeSSH(host == "localhost")
    , keyFile(keyFile)
    , sshPublicHostKey(sshPublicHostKey)
    , compress(compress)
    , logFD(logFD)
{
    if (host == "" || host.starts_with("-"))
        throw Error("invalid SSH host name '%s'", host);

    auto state(state_.lock());
    state->tmpDir = std::make_unique<AutoDelete>(createTempDir("", "nix", true, true, 0700));
}

void SSH::addCommonSSHOpts(Strings & args)
{
    auto state(state_.lock());

    if (port.has_value())
        args.insert(args.end(), {"-p", std::to_string(*port)});
    for (auto & i : tokenizeString<Strings>(getEnv("NIX_SSHOPTS").value_or("")))
        args.push_back(i);
    if (!keyFile.empty())
        args.insert(args.end(), {"-i", keyFile});
    if (!sshPublicHostKey.empty()) {
        Path fileName = (Path) *state->tmpDir + "/host-key";
        auto p = host.rfind("@");
        std::string thost = p != std::string::npos ? std::string(host, p + 1) : host;
        writeFile(fileName, thost + " " + base64Decode(sshPublicHostKey) + "\n");
        args.insert(args.end(), {"-oUserKnownHostsFile=" + fileName});
    }
    if (compress)
        args.push_back("-C");
}

std::unique_ptr<SSH::Connection> SSH::startCommand(const std::string & command)
{
    int sp[2];
    // only linux and bsd support SOCK_CLOEXEC in socketpair type.
#if __linux__ || __FreeBSD__
    constexpr int sock_type = SOCK_STREAM | SOCK_CLOEXEC;
#else
    constexpr int sock_type = SOCK_STREAM;
#endif
    if (socketpair(AF_UNIX, sock_type, 0, sp) < 0) {
        throw SysError("socketpair() for ssh");
    }

    AutoCloseFD parent(sp[0]), child(sp[1]);
#if !(__linux__ || __FreeBSD__)
    if (fcntl(parent.get(), F_SETFD, O_CLOEXEC) < 0 || fcntl(child.get(), F_SETFD, O_CLOEXEC) < 0) {
        throw SysError("making socketpair O_CLOEXEC");
    }
#endif

    auto conn = std::make_unique<Connection>();
    ProcessOptions options;
    options.dieWithParent = false;

    std::optional<Finally<std::function<void()>>> resumeLoggerDefer;
    if (!fakeSSH) {
        logger->pause();
        resumeLoggerDefer.emplace([&]() { logger->resume(); });
    }

    conn->sshPid = startProcess([&]() {
        restoreProcessContext();

        parent.close();

        if (dup2(child.get(), STDIN_FILENO) == -1) {
            throw SysError("duping over stdin");
        }
        if (dup2(child.get(), STDOUT_FILENO) == -1) {
            throw SysError("duping over stdout");
        }
        if (logFD != -1 && dup2(logFD, STDERR_FILENO) == -1)
            throw SysError("duping over stderr");

        Strings args;

        // We specifically spawn bash here, to (hopefully) get
        // reasonably POSIX-y semantics for the things we're about
        // to do next.
        if (fakeSSH) {
            args = { "bash", "-c", command };
        } else {
            args = { "ssh", host.c_str(), "-x", "-T" };
            addCommonSSHOpts(args);
            args.push_back(command);
        }

        execvp(args.begin()->c_str(), stringsToCharPtrs(args).data());

        // could not exec ssh/bash
        throw SysError("unable to execute '%s'", args.front());
    }, options);

    child.close();

    conn->socket = std::move(parent);

    return conn;
}

}
