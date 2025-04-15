#pragma once
///@file

#include "lix/libutil/file-system.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/sync.hh"
#include <cstdint>

namespace nix {

class SSH
{
private:

    const std::string host;
    const std::optional<uint16_t> port;
    bool fakeSSH;
    const std::string keyFile;
    const std::string sshPublicHostKey;
    const bool compress;
    const int logFD;

    struct State
    {
        std::unique_ptr<AutoDelete> tmpDir;
    };

    Sync<State> state_;

    void addCommonSSHOpts(Strings & args);

public:

    SSH(const std::string & host, const std::optional<uint16_t> port, const std::string & keyFile, const std::string & sshPublicHostKey, bool compress, int logFD = -1);

    struct Connection
    {
        Pid sshPid;
        AutoCloseFD out, in;
    };

    std::unique_ptr<Connection> startCommand(const std::string & command);
};

}
