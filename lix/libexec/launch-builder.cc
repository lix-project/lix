#include "launch-builder.hh"
#include "lix/libstore/build/request.capnp.h"
#include "lix/libutil/rpc.hh"
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <csignal>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <grp.h>
#include <sys/resource.h>
#include <unistd.h>
#include <vector>

namespace nix {
bool printDebugLogs = false;

static void requireCString(const char * context, const std::string & s)
{
    if (s.contains('\0')) {
        std::string p{s};
        for (auto pos = p.find('\0'); pos != p.npos; pos = p.find('\0')) {
            p.replace(pos, 1, "␀");
        }
        // NOLINTNEXTLINE(lix-foreign-exceptions)
        throw std::runtime_error(std::format("derivation {} {} contains NUL bytes", context, p));
    }
}

ExecRequest::ExecRequest(build::Request::Reader request)
{
    const auto fill = [](auto context, auto & strings, auto & pointers, auto from) {
        strings.reserve(from.size());
        for (auto arg : from) {
            strings.push_back(rpc::to<std::string>(arg));
            requireCString(context, strings.back());
            pointers.push_back(strings.back().data());
        }
        pointers.push_back(nullptr);
    };

    builder = rpc::to<std::string>(request.getBuilder());
    requireCString("derivation builder", builder);

    fill("derivation argument", argsStorage, args, request.getArgs());
    fill("derivation environment entry", envsStorage, envs, request.getEnvironment());
}

void writeFull(int fd, std::string_view data)
{
    while (!data.empty()) {
        const auto wrote = ::write(fd, data.data(), data.size());
        if (wrote < 0) {
            throw SysError("write()");
        } else {
            data.remove_prefix(size_t(wrote));
        }
    }
}

static void closeExtraFDs()
{
    constexpr int MAX_KEPT_FD = 2;
    static_assert(std::max({STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) == MAX_KEPT_FD);

    // Both Linux and FreeBSD support close_range.
#if __linux__ || __FreeBSD__
    auto closeRange = [](unsigned int first, unsigned int last, int flags) -> int {
    // musl does not have close_range as of 2024-08-10
    // patch: https://www.openwall.com/lists/musl/2024/08/01/9
#if HAVE_CLOSE_RANGE
        return close_range(first, last, flags);
#else
        return syscall(SYS_close_range, first, last, flags);
#endif
    };
    // first try to close_range everything we don't care about. if this
    // returns an error with these parameters we're running on a kernel
    // that does not implement close_range (i.e. pre 5.9) and fall back
    // to the old method. we should remove that though, in some future.
    if (closeRange(3, ~0U, 0) == 0) {
        return;
    }
#endif

#if __linux__
    try {
        for (auto & s : std::filesystem::directory_iterator("/proc/self/fd")) {
            auto fd = std::stoi(s.path().filename().c_str());
            if (fd > MAX_KEPT_FD) {
                debug("closing leaked FD %d", fd);
                close(fd);
            }
        }
        return;
    } catch (std::exception &) { // NOLINT(lix-foreign-exceptions): that's what std::filesystem throws
    }
#endif

    int maxFD = 0;
    maxFD = sysconf(_SC_OPEN_MAX);
    for (int fd = MAX_KEPT_FD + 1; fd < maxFD; ++fd) {
        close(fd); /* ignore result */
    }
}
}

int main(int argc, char * argv[])
{
    using namespace nix;

    if (argc < 1) {
        return 255;
    }

    bool sendException = true;

    try {
        capnp::StreamFdMessageReader reader(STDIN_FILENO);

        auto request = reader.getRoot<build::Request>();

        printDebugLogs = request.getDebug();

        {
            sigset_t set;
            sigemptyset(&set);
            if (sigprocmask(SIG_SETMASK, &set, nullptr)) {
                throw SysError("failed to unmask signals");
            }
        }

        /* Put the child in a separate session (and thus a separate
           process group) so that it has no controlling terminal (meaning
           that e.g. ssh cannot open /dev/tty) and it doesn't receive
           terminal signals. */
        if (setsid() == -1) {
            throw SysError("creating a new session");
        }

        /* Dup stderr to stdout. */
        if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1) {
            throw SysError("cannot dup stderr into stdout");
        }

        /* Reroute stdin to /dev/null. */
        kj::AutoCloseFd fdDevNull{open("/dev/null", O_RDWR)};
        if (fdDevNull == nullptr) {
            throw SysError("cannot open /dev/null");
        }
        if (dup2(fdDevNull.get(), STDIN_FILENO) == -1) {
            throw SysError("cannot dup null device into stdin");
        }

        const bool setUser = prepareChildSetup(request);

        // NOLINTNEXTLINE(lix-unsafe-c-calls): we trust the parent here
        if (chdir(rpc::to<std::string>(request.getWorkingDir()).c_str()) == -1) {
            throw SysError("changing into %s", rpc::to<std::string>(request.getWorkingDir()));
        }

        /* Disable core dumps by default. */
        struct rlimit limit = {0, RLIM_INFINITY};
        if (request.getEnableCoreDumps()) {
            limit.rlim_cur = RLIM_INFINITY;
        }
        setrlimit(RLIMIT_CORE, &limit);

        // FIXME: set other limits to deterministic values?

        /* If we are running in `build-users' mode, then switch to the
           user we allocated above.  Make sure that we drop all root
           privileges.  Note that above we have closed all file
           descriptors except std*, so that's safe.  Also note that
           setuid() when run as root sets the real, effective and
           saved UIDs. */
        if (setUser && request.hasCredentials()) {
            auto creds = request.getCredentials();
            /* Preserve supplementary groups of the build user, to allow
               admins to specify groups such as "kvm".  */
            std::vector<gid_t> gids;
            std::copy(
                creds.getSupplementaryGroups().begin(),
                creds.getSupplementaryGroups().end(),
                std::back_inserter(gids)
            );
            if (setgroups(gids.size(), gids.data()) == -1) {
                throw SysError("cannot set supplementary groups of build user");
            }

            if (setgid(creds.getGid()) == -1 || getgid() != creds.getGid() || getegid() != creds.getGid()) {
                throw SysError("setgid failed");
            }

            if (setuid(creds.getUid()) == -1 || getuid() != creds.getUid() || geteuid() != creds.getUid()) {
                throw SysError("setuid failed");
            }
        }

        finishChildSetup(request);

        /* Indicate that we managed to set up the build environment. */
        writeFull(STDERR_FILENO, std::string("\2\n"));

        /* Close all other file descriptors. */
        closeExtraFDs();

        sendException = false;

        execBuilder(request);
    } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
        if (sendException) {
            writeFull(STDERR_FILENO, std::format("\1{}\n", e.what()));
        } else {
            writeFull(STDERR_FILENO, e.what());
        }
        return 1;
    }
}
