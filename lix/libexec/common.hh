#pragma once
///@file common setup/utility header for libexec helpers

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <format> // IWYU pragma: keep
#include <span>
#include <string> // IWYU pragma: keep
#include <string_view>
#include <unistd.h>

/// file descriptor of the error reporting pipe. anything written to this pipe
/// will be treated as a fatal error message regardless of helper exit status.
/// an empty line (a single `\n` byte) will be treated as successful startup,
/// any errors encountered later can be retrieved by the parent in due course.
inline int ERR_PIPE;

inline void writeErrPipe(std::string_view msg)
{
    while (!msg.empty()) {
        if (auto wrote = write(ERR_PIPE, msg.data(), msg.size()); wrote >= 0) {
            msg.remove_prefix(size_t(wrote));
        } else {
            break;
        }
    }
}

/// immediately terminate helper execution with a fatal error.
[[noreturn]]
inline void die(std::string_view msg)
{
    writeErrPipe(msg);
    exit(252);
}

/// check syscall result and immediately terminate with a message on failure.
#define DIE_UNLESS_SYS(name, expr)                             \
    ([&] {                                                     \
        if ((expr) == -1) {                                    \
            die(std::format("{}: {}", name, strerror(errno))); \
        }                                                      \
    }())

/// declare the TU expanding this as a libexec helper with at least `expectedArgs`
/// arguments. more arguments may be passed, fewer args will be treated as a fatal
/// error and reported immediately. a valid ERR_PIPE pipe must be passed as as the
/// first argument and will be set to close-on-exec to not pass it on to children.
#define LIBEXEC_HELPER(expectedArgs)                                                     \
    int main(int argc, char * argv[])                                                    \
    {                                                                                    \
        if (argc < (expectedArgs) + 2) {                                                 \
            _exit(254);                                                                  \
        }                                                                                \
                                                                                         \
        try {                                                                            \
            /* NOTE: we purposely accept imperfect conversion, only errors are fatal.    \
               if our parent messes this up we have *much* bigger problems than this. */ \
            ERR_PIPE = std::stoi(argv[1]);                                               \
        } catch (...) {                                                                  \
            _exit(253);                                                                  \
        }                                                                                \
                                                                                         \
        DIE_UNLESS_SYS("error pipe fcntl", fcntl(ERR_PIPE, F_SETFD, FD_CLOEXEC));        \
        return helperMain(argv[0], {argv + 2, argv + argc});                             \
    }

int helperMain(const char * name, std::span<char *> args) noexcept;
