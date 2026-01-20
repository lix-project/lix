#include "common.hh"
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <format>
#include <sched.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/wait.h>

LIBEXEC_HELPER(0)

static int waitFor(pid_t child)
{
    int status;
    while (true) {
        if (waitpid(child, &status, 0) == -1) {
            if (errno != EINTR) {
                DIE_UNLESS_SYS("waitpid()", -1);
            }
        } else if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            die(std::format("child died with signal {}", WTERMSIG(status)));
        } else {
            die(std::format("child exited {}", status));
        }
    }
}

int helperMain(const char * name, std::span<char *> args) noexcept
{
    size_t stackSize = 1ul * 1024 * 1024;
    auto stack = static_cast<char *>(
        mmap(0, stackSize, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0)
    );
    if (stack == MAP_FAILED) {
        die(std::format("mmap(): {}", strerror(errno)));
    }

    const bool haveUserNS = [&] {
        auto child = clone([](void *) { return 0; }, stack + stackSize, CLONE_NEWUSER | SIGCHLD, nullptr);
        if (child == -1) {
            printf("user %s\n", strerror(errno));
            return false;
        } else if (auto status = waitFor(child)) {
            die(std::format("userns check child failed unexpectedly with status {}", status));
        } else {
            printf("user\n");
            return true;
        }
    }();

    {
        auto child = clone(
            [](void *) {
                /* Make sure we don't remount the parent's /proc. */
                if (mount(0, "/", 0, MS_PRIVATE | MS_REC, 0) == -1) {
                    return 1;
                }

                /* Test whether we can remount /proc. The kernel disallows
                   this if /proc is not fully visible, i.e. if there are
                   filesystems mounted on top of files inside /proc.  See
                   https://lore.kernel.org/lkml/87tvsrjai0.fsf@xmission.com/T/. */
                if (mount("none", "/proc", "proc", 0, 0) == -1) {
                    return 2;
                }

                return 0;
            },
            stack + stackSize,
            CLONE_NEWNS | CLONE_NEWPID | (haveUserNS ? CLONE_NEWUSER : 0) | SIGCHLD,
            nullptr
        );
        if (child == -1) {
            printf("mount-pid %s\n", strerror(errno));
        } else if (waitFor(child) != 0) {
            printf("mount-pid failed to remount /proc\n");
        } else {
            printf("mount-pid\n");
        }
    }

    return 0;
}
