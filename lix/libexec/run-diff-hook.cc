#include "common.hh"

#include <grp.h>

using std::literals::operator""sv;

LIBEXEC_HELPER(3)

int helperMain(const char * name, std::span<char *> args) noexcept
{
    const auto uid = args[0];
    const auto gid = args[1];
    const auto hook = args.subspan(2);

    DIE_UNLESS_SYS("chdir", chdir("/"));
    if (gid != "-"sv) {
        DIE_UNLESS_SYS("setgid", setgid(argToInt<gid_t>("gid", gid)));
        /* Drop all other groups if we're setgid. */
        DIE_UNLESS_SYS("setgroups", setgroups(0, 0));
    }
    if (uid != "-"sv) {
        DIE_UNLESS_SYS("setuid", setuid(argToInt<uid_t>("uid", uid)));
    }

    execvp(hook[0], hook.data());
    die("exec failed");
}
