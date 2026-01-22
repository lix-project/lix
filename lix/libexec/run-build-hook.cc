#include "common.hh"
#include <unistd.h>

LIBEXEC_HELPER(2)

int helperMain(const char * name, std::span<char *> args) noexcept
{
    DIE_UNLESS_SYS("chdir", chdir("/"));
    DIE_UNLESS_SYS("setsid", setsid());

    static_assert(STDIN_FILENO == 0);
    DIE_UNLESS_SYS("close(stdin)", close(STDIN_FILENO));
    DIE_UNLESS_SYS("stdin = open(/dev/null)", open("/dev/null", O_RDWR));

    execv(args[0], args.subspan(1).data());
    die("exec failed");
}
