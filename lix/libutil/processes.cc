#include "async-io.hh"
#include "c-calls.hh"
#include "file-descriptor.hh"
#include "lix/libutil/current-process.hh"
#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/signals.hh"
#include "manually-drop.hh"
#include "sync.hh"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>

#include <grp.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

// both apple and linux must use raw syscalls for some things that are not exposed otherwise
#include <sys/syscall.h>

#ifdef __linux__
# include <linux/capability.h>
# include <sys/prctl.h>
# include <sys/mman.h>
#endif


namespace nix {

Pid::Pid()
{
}

Pid::Pid(Pid && other) : pid(other.pid)
{
    other.pid = -1;
}


Pid & Pid::operator=(Pid && other)
{
    Pid tmp(std::move(other));
    std::swap(pid, tmp.pid);
    return *this;
}


Pid::~Pid() noexcept(false)
{
    if (pid != -1) kill();
}


int Pid::kill()
{
    assert(pid != -1);

    debug("killing process %1%", pid);

    /* Send the requested signal to the child. */
    if (::kill(pid, SIGKILL) != 0) {
        logError(SysError("killing process %d", pid).info());
    }

    return wait();
}


int Pid::wait()
{
    assert(pid != -1);
    while (1) {
        int status;
        int res = waitpid(pid, &status, 0);
        if (res == pid) {
            pid = -1;
            return status;
        }
        if (errno != EINTR)
            throw SysError("cannot get exit status of PID %d", pid);
        checkInterrupt();
    }
}

pid_t Pid::release()
{
    pid_t p = pid;
    pid = -1;
    return p;
}

int ProcessGroup::kill()
{
    assert(leader);

    debug("killing process group %1%", leader.get());

    /* Send the requested signal to every process in the child
       process group (which hopefully includes *all* its children). */
    if (::kill(-leader.get(), SIGKILL) != 0) {
        /* On BSDs, killing a process group will return EPERM if all
           processes in the group are zombies (or something like
           that). So try to detect and ignore that situation. */
#if __FreeBSD__ || __APPLE__
        if (errno != EPERM || ::kill(leader.get(), 0) != 0)
#endif
            logError(SysError("killing process group %d", leader.get()).info());
    }

    return wait();
}

ProcessGroup::~ProcessGroup() noexcept(false)
{
    if (leader) {
        kill();
    }
}

void killUser(uid_t uid)
{
    debug("killing all processes running under uid '%1%'", uid);

    assert(uid != 0); /* just to be safe... */

    runHelper("kill-user", {.args = {std::to_string(uid)}}).waitAndCheck();
}


//////////////////////////////////////////////////////////////////////


static pid_t doFork(std::function<void()> fun)
{
    pid_t pid = fork();
    if (pid != 0) return pid;
    fun();
    abort();
}

#if __linux__
static int childEntry(void * arg)
{
    auto main = static_cast<std::function<void()> *>(arg);
    (*main)();
    return 1;
}
#endif


Pid startProcess(std::function<void()> fun, const ProcessOptions & options)
{
    std::function<void()> wrapper = [&]() {
        logger = makeSimpleLogger();
        try {
            fun();
        } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
            try {
                std::cerr << e.what() << "\n";
            } catch (...) { }
        } catch (...) { }
        _exit(1);
    };

    pid_t pid = -1;

    if (options.cloneFlags) {
        #ifdef __linux__
        // Not supported, since then we don't know when to free the stack.
        assert(!(options.cloneFlags & CLONE_VM));

        size_t stackSize = 1ul * 1024 * 1024;
        auto stack = static_cast<char *>(mmap(0, stackSize,
            PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0));
        if (stack == MAP_FAILED) throw SysError("allocating stack");

        Finally freeStack([&]() { munmap(stack, stackSize); });

        pid = clone(childEntry, stack + stackSize, options.cloneFlags | SIGCHLD, &wrapper);
        #else
        throw Error("clone flags are only supported on Linux");
        #endif
    } else
        pid = doFork(wrapper);

    if (pid == -1) throw SysError("unable to fork");

    return Pid{pid};
}

kj::Promise<Result<std::string>>
runProgram(Path program, bool searchPath, const Strings args, bool isInteractive)
try {
    // allow only one interactive program per unit time so they don't mess with each other.
    //
    // see https://git.lix.systems/lix-project/lix/issues/702 for why this is ManuallyDrop.
    static ManuallyDrop<Sync<int, AsyncMutex>> interactiveMutex{std::in_place, 0};

    std::optional<Sync<int, AsyncMutex>::Lock> interactiveLock;
    KJ_DEFER({
        if (interactiveLock) {
            logger->resume();
        }
    });

    if (isInteractive) {
        interactiveLock = co_await interactiveMutex->lock();
        logger->pause();
    }

    auto res = TRY_AWAIT(runProgram(RunOptions{
        .program = program,
        .searchPath = searchPath,
        .args = args,
    }));

    if (!statusOk(res.first)) {
        throw ExecError(res.first, "program '%1%' %2%", program, statusToString(res.first));
    }

    co_return res.second;
} catch (...) {
    co_return result::current_exception();
}

// Output = error code + "standard out" output stream
kj::Promise<Result<std::pair<int, std::string>>> runProgram(RunOptions options)
try {
    options.captureStdout = true;

    int status = 0;
    std::string childStdout;

    try {
        auto proc = runProgram2(options);
        Finally const _wait([&] { proc.waitAndCheck(); });
        childStdout = TRY_AWAIT(proc.getStdout()->drain());
    } catch (ExecError & e) {
        status = e.status;
    }

    co_return {status, std::move(childStdout)};
} catch (...) {
    co_return result::current_exception();
}

RunningProgram::RunningProgram(PathView program, Pid pid, AutoCloseFD childStdout)
    : program(program)
    , pid(std::move(pid))
    , childStdout(childStdout ? std::make_unique<AsyncFdIoStream>(std::move(childStdout)) : nullptr)
{
}

RunningProgram::~RunningProgram()
{
    if (pid) {
        // we will not kill a subprocess because we *can't* kill a subprocess
        // reliably without placing it in its own process group, and then too
        // we could not be sure to terminate the entire subprocess hierarchy.
        assert(false && "destroying un-wait()ed running process");
        std::terminate();
    }
}

std::tuple<Pid, std::unique_ptr<AsyncFdIoStream>> RunningProgram::release()
{
    return {std::move(pid), std::move(childStdout)};
}

int RunningProgram::kill()
{
    return pid.kill();
}

int RunningProgram::wait()
{
    return pid.wait();
}

int RunningHelper::killProcessGroup()
{
    return ProcessGroup{std::move(pid)}.kill();
}

void RunningProgram::waitAndCheck()
{
    if (std::uncaught_exceptions() == 0) {
        int status = pid.wait();
        if (status)
            throw ExecError(status, "program '%1%' %2%", program, statusToString(status));
    } else {
        pid.kill();
        debug("killed subprocess %1% during exception handling", program);
    }
}

void RunningHelper::check()
{
    char first;
    try {
        readFull(errPipe.get(), &first, 1);
    } catch (EndOfFile &) {
        return;
    }
    if (first == '\n') {
        return;
    }
    auto rest = readFile(errPipe.get());
    int status = kill();
    throw ExecError(status, "helper %s failed: %s", name, first + rest);
}

void RunningHelper::waitAndCheck()
{
    if (std::uncaught_exceptions() == 0) {
        auto error = readFile(errPipe.get());
        if (!error.empty()) {
            int status = kill();
            throw ExecError(status, "helper %s failed: %s", name, error);
        }
        RunningProgram::waitAndCheck();
    } else {
        RunningProgram::kill();
    }
}

RunningProgram runProgram2(const RunOptions & options)
{
    checkInterrupt();

    /* Create a pipe. */
    Pipe out;
    if (options.captureStdout) out.create();

    ProcessOptions processOptions{};

    printMsg(lvlChatty, "running command: %s", concatMapStringsSep(" ", options.args, shellEscape));

    /* Fork. */
    Pid pid{startProcess(
        [&]() {
            if (options.environment) {
                replaceEnv(*options.environment);
            }
            if (options.captureStdout && dup2(out.writeSide.get(), STDOUT_FILENO) == -1) {
                throw SysError("dupping stdout");
            }
            for (auto redirection : options.redirections) {
                if (redirection.dup == redirection.from) {
                    if (int flags = fcntl(redirection.from, F_GETFD);
                        flags < 0 || fcntl(redirection.from, F_SETFD, flags & ~FD_CLOEXEC) < 0)
                    {
                        throw SysError("clearing O_CLOEXEC of fd %i", redirection.dup);
                    }
                } else if (dup2(redirection.from, redirection.dup) == -1) {
                    throw SysError("dupping fd %i to %i", redirection.dup, redirection.from);
                }
            }

            Strings args_(options.args);
            args_.push_front(options.argv0.value_or(options.program));

            restoreProcessContext();

            if (options.searchPath) {
                sys::execvp(options.program, args_);
                // This allows you to refer to a program with a pathname relative
                // to the PATH variable.
            } else
                sys::execv(options.program, args_);

            throw SysError("executing '%1%'", options.program);
        },
        processOptions
    )};

    out.writeSide.close();

    return RunningProgram{
        options.program,
        std::move(pid),
        options.captureStdout ? std::move(out.readSide) : AutoCloseFD{}
    };
}

RunningHelper runHelper(const char * name, RunOptions options)
{
    Pipe errPipe;
    errPipe.create();

    options.program = fmt("%s/%s", LIX_LIBEXEC_DIR, name);
    options.args.push_front(std::to_string(errPipe.writeSide.get()));
    options.searchPath = false;
    options.redirections.push_back({.dup = errPipe.writeSide.get(), .from = errPipe.writeSide.get()});

    RunningHelper helper{name, runProgram2(options), std::move(errPipe.readSide)};
    errPipe.writeSide.close();
    helper.check();
    return helper;
}

std::string statusToString(int status)
{
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFEXITED(status))
            return fmt("failed with exit code %1%", WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
#if HAVE_STRSIGNAL
            const char * description = strsignal(sig);
            return fmt("failed due to signal %1% (%2%)", sig, description);
#else
            return fmt("failed due to signal %1%", sig);
#endif
        }
        else
            return "died abnormally";
    } else return "succeeded";
}


bool statusOk(int status)
{
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}
