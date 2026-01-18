#include "common.hh"

LIBEXEC_HELPER(0)

int helperMain(const char * name, std::span<char *> args) noexcept
{
    auto pager = args.empty() ? nullptr : args[0];

    if (!getenv("LESS")) {
        setenv("LESS", "FRSXMK", 1);
    }
    if (pager) {
        execl("/bin/sh", "sh", "-c", pager, nullptr);
    }
    execlp("pager", "pager", nullptr);
    execlp("less", "less", nullptr);
    execlp("more", "more", nullptr);
    die("could not find a pager to run, please set PAGER or NIX_PAGER");
}
