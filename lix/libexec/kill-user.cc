#include "common.hh"
#include <charconv>
#include <cstring>
#include <format>
#include <signal.h>
#include <unistd.h>

#if __APPLE__
#include <sys/syscall.h>
#endif

LIBEXEC_HELPER(1)

int helperMain(const char * name, std::span<char *> args) noexcept
{
    std::string_view uidArg = args[0];
    uid_t uid;

    if (auto res = std::from_chars(uidArg.begin(), uidArg.end(), uid);
        res.ptr != uidArg.end() || res.ec != std::errc())
    {
        die("invalid uid argument");
    }

    /* The system call kill(-1, sig) sends the signal `sig' to all
       users to which the current process can send signals.  So we
       switch to that uid and send a mass kill once we've done so. */

    if (setuid(uid) == -1) {
        die(std::format("setuid(): {}", strerror(errno)));
    }

    while (true) {
#ifdef __APPLE__
        /* OSX's kill syscall takes a third parameter that, among
           other things, determines if kill(-1, signo) affects the
           calling process. In the OSX libc, it's set to true,
           which means "follow POSIX", which we don't want here */
        if (syscall(SYS_kill, -1, SIGKILL, false) == 0) {
            break;
        }
#else
        if (kill(-1, SIGKILL) == 0) {
            break;
        }
#endif
        if (errno == ESRCH || errno == EPERM) {
            break; /* no more processes */
        }
        if (errno != EINTR) {
            die(std::format("cannot kill processes for uid {}: {}", uid, strerror(errno)));
        }
    }

    /* !!! We should really do some check to make sure that there are
       no processes left running under `uid', but there is no portable
       way to do so (I think).  The most reliable way may be `ps -eo
       uid | grep -q $uid'. */
    return 0;
}
