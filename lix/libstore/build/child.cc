#include "lix/libutil/c-calls.hh"
#include "lix/libutil/current-process.hh"
#include "lix/libutil/logging.hh"

namespace nix {

void commonExecveingChildInit()
{
    logger = makeSimpleLogger();

    const static std::string pathNullDevice = "/dev/null";
    restoreProcessContext(false);

    /* Put the child in a separate session (and thus a separate
       process group) so that it has no controlling terminal (meaning
       that e.g. ssh cannot open /dev/tty) and it doesn't receive
       terminal signals. */
    if (setsid() == -1)
        throw SysError("creating a new session");

    /* Dup stderr to stdout. */
    if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1)
        throw SysError("cannot dup stderr into stdout");

    /* Reroute stdin to /dev/null. */
    auto fdDevNull = sys::open(pathNullDevice, O_RDWR);
    if (!fdDevNull) {
        throw SysError("cannot open '%1%'", pathNullDevice);
    }
    if (dup2(fdDevNull.get(), STDIN_FILENO) == -1) {
        throw SysError("cannot dup null device into stdin");
    }
}

}
