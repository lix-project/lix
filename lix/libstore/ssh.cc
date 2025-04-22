#include "lix/libutil/current-process.hh"
#include "lix/libutil/environment-variables.hh"
#include "lix/libstore/ssh.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/strings.hh"
#include "lix/libstore/temporary-dir.hh"

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
    Pipe in, out;
    in.create();
    out.create();

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

        close(in.writeSide.get());
        close(out.readSide.get());

        if (dup2(in.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("duping over stdin");
        if (dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw SysError("duping over stdout");
        if (logFD != -1 && dup2(logFD, STDERR_FILENO) == -1)
            throw SysError("duping over stderr");

        Strings args;

        // We specifically spawn bash here, to (hopefully) get
        // reasonably POSIX-y semantics for the things we're about
        // to do next.
        if (fakeSSH) {
            args = { "bash" };
        } else {
            args = { "ssh", host.c_str(), "-x", "-T", "-oRemoteCommand=bash" };
            addCommonSSHOpts(args);
        }

        execvp(args.begin()->c_str(), stringsToCharPtrs(args).data());

        // could not exec ssh/bash
        throw SysError("unable to execute '%s'", args.front());
    }, options);


    in.readSide.reset();
    out.writeSide.reset();

    // Once we hand off to nix-store (on the remote) and the caller (on the client),
    // we lose the ability to catch SSH failing, due to Historical Architectural Decisions.
    //
    // We want to catch at least _some_ errors and alert the user in case of
    // an obvious misconfiguration, so run a very simple command first
    // to make sure things are at least somewhat operational.
    //
    // The exact semantics of
    // - not having a shell prompt get in the way when non-interactive
    // - echo doing the reasonable thing
    // Are exactly why we specifically forced bash (via ssh RemoteCommand) earlier.
    // We do *not* use /bin/sh because that may be busybox and busybox breaks here.
    //
    // FIXME: make any of this shit make sense
    {
        writeLine(in.writeSide.get(), "echo started");

        std::string reply;
        try {
            reply = readLine(out.readSide.get());
        } catch (EndOfFile & e) { }

        if (reply != "started") {
            warn("SSH to '%s' failed, stdout first line: '%s'", host, reply);
            throw Error("failed to start SSH connection to '%s'", host);
        }
    }

    // Now that we're reasonably confident we have something vaguely resembling
    // a connection, hand off to the command.
    writeLine(in.writeSide.get(), fmt("exec %s", command));

    conn->out = std::move(out.readSide);
    conn->in = std::move(in.writeSide);

    return conn;
}

}
