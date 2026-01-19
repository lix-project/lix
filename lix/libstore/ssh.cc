#include "lix/libutil/c-calls.hh"
#include "lix/libutil/current-process.hh"
#include "lix/libutil/environment-variables.hh"
#include "lix/libstore/ssh.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/processes.hh"
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
    state->tmpDir = std::make_unique<AutoDelete>(createTempDir("nix", 0700));
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
    auto [parent, child] = SocketPair::stream();
    auto conn = std::make_unique<Connection>();

    std::optional<Finally<std::function<void()>>> resumeLoggerDefer;
    if (!fakeSSH) {
        logger->pause();
        resumeLoggerDefer.emplace([&]() { logger->resume(); });
    }

    RunOptions options;

    // We specifically spawn bash here, to (hopefully) get
    // reasonably POSIX-y semantics for the things we're about
    // to do next.
    if (fakeSSH) {
        options.program = "bash";
        options.args = {"-c", command};
    } else {
        options.program = "ssh";
        options.args = {host.c_str(), "-x", "-T"};
        addCommonSSHOpts(options.args);
        options.args.push_back(command);
    }

    options.redirections.push_back({.dup = STDIN_FILENO, .from = child.get()});
    options.redirections.push_back({.dup = STDOUT_FILENO, .from = child.get()});
    if (logFD != -1) {
        options.redirections.push_back({.dup = STDERR_FILENO, .from = logFD});
    }

    auto [pid, _stdout] = runProgram2(options).release();
    conn->sshPid = std::move(pid);

    child.close();

    conn->socket = std::move(parent);

    return conn;
}

}
