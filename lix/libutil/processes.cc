#include "async-io.hh"
#include "lix/libutil/current-process.hh"
#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/finally.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/signals.hh"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>

#include <grp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
# include <sys/syscall.h>
#endif

#ifdef __linux__
# include <linux/capability.h>
# include <sys/prctl.h>
# include <sys/mman.h>
#endif


namespace nix {

Pid::Pid()
{
}


Pid::Pid(Pid && other) : pid(other.pid), separatePG(other.separatePG), killSignal(other.killSignal)
{
    other.pid = -1;
}


Pid & Pid::operator=(Pid && other)
{
    Pid tmp(std::move(other));
    std::swap(pid, tmp.pid);
    std::swap(separatePG, tmp.separatePG);
    std::swap(killSignal, tmp.killSignal);
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

    /* Send the requested signal to the child.  If it has its own
       process group, send the signal to every process in the child
       process group (which hopefully includes *all* its children). */
    if (::kill(separatePG ? -pid : pid, killSignal) != 0) {
        /* On BSDs, killing a process group will return EPERM if all
           processes in the group are zombies (or something like
           that). So try to detect and ignore that situation. */
#if __FreeBSD__ || __APPLE__
        if (errno != EPERM || ::kill(pid, 0) != 0)
#endif
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


void Pid::setSeparatePG(bool separatePG)
{
    this->separatePG = separatePG;
}


void Pid::setKillSignal(int signal)
{
    this->killSignal = signal;
}


pid_t Pid::release()
{
    pid_t p = pid;
    pid = -1;
    return p;
}


void killUser(uid_t uid)
{
    debug("killing all processes running under uid '%1%'", uid);

    assert(uid != 0); /* just to be safe... */

    /* The system call kill(-1, sig) sends the signal `sig' to all
       users to which the current process can send signals.  So we
       fork a process, switch to uid, and send a mass kill. */

    Pid pid{startProcess([&]() {

        if (setuid(uid) == -1)
            throw SysError("setting uid");

        while (true) {
#ifdef __APPLE__
            /* OSX's kill syscall takes a third parameter that, among
               other things, determines if kill(-1, signo) affects the
               calling process. In the OSX libc, it's set to true,
               which means "follow POSIX", which we don't want here
                 */
            if (syscall(SYS_kill, -1, SIGKILL, false) == 0) break;
#else
            if (kill(-1, SIGKILL) == 0) break;
#endif
            if (errno == ESRCH || errno == EPERM) break; /* no more processes */
            if (errno != EINTR)
                throw SysError("cannot kill processes for uid '%1%'", uid);
        }

        _exit(0);
    })};

    int status = pid.wait();
    if (status != 0)
        throw Error("cannot kill processes for uid '%1%': %2%", uid, statusToString(status));

    /* !!! We should really do some check to make sure that there are
       no processes left running under `uid', but there is no portable
       way to do so (I think).  The most reliable way may be `ps -eo
       uid | grep -q $uid'. */
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
#if __linux__
            if (options.dieWithParent && prctl(PR_SET_PDEATHSIG, SIGKILL) == -1)
                throw SysError("setting death signal");
#endif
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

        size_t stackSize = 1 * 1024 * 1024;
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
    auto res = TRY_AWAIT(runProgram(RunOptions{
        .program = program, .searchPath = searchPath, .args = args, .isInteractive = isInteractive
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
        // reliably without placing it in its own process group, and cleaning
        // up a subprocess only when `separatePG` is set is a loaded footgun.
        assert(false && "destroying un-wait()ed running process");
        std::terminate();
    }
}

std::tuple<pid_t, std::unique_ptr<AsyncFdIoStream>> RunningProgram::release()
{
    return {pid.release(), std::move(childStdout)};
}

int RunningProgram::kill()
{
    return pid.kill();
}

int RunningProgram::wait()
{
    return pid.wait();
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

RunningProgram runProgram2(const RunOptions & options)
{
    checkInterrupt();

    /* Create a pipe. */
    Pipe out;
    if (options.captureStdout) out.create();

    ProcessOptions processOptions {
        .dieWithParent = options.dieWithParent,
    };

    std::optional<Finally<std::function<void()>>> resumeLoggerDefer;
    if (options.isInteractive) {
        logger->pause();
        resumeLoggerDefer.emplace(
            []() {
                logger->resume();
            }
        );
    }

    printMsg(lvlChatty, "running command: %s", concatMapStringsSep(" ", options.args, shellEscape));

    /* Fork. */
    Pid pid{startProcess([&]() {
        if (options.environment)
            replaceEnv(*options.environment);
        if (options.captureStdout && dup2(out.writeSide.get(), STDOUT_FILENO) == -1)
            throw SysError("dupping stdout");
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

        if (options.chdir && chdir((*options.chdir).c_str()) == -1)
            throw SysError("chdir failed");

#if __linux__
        if (!options.caps.empty() && prctl(PR_SET_KEEPCAPS, 1) < 0) {
            throw SysError("setting keep-caps failed");
        }
#endif

        if (options.gid && setgid(*options.gid) == -1)
            throw SysError("setgid failed");
        /* Drop all other groups if we're setgid. */
        if (options.gid && setgroups(0, 0) == -1)
            throw SysError("setgroups failed");
        if (options.uid && setuid(*options.uid) == -1)
            throw SysError("setuid failed");

#if __linux__
        if (!options.caps.empty()) {
            if (prctl(PR_SET_KEEPCAPS, 0)) {
                throw SysError("clearing keep-caps failed");
            }

            // we do the capability dance like this to avoid a dependency
            // on libcap, which has a rather large build closure and many
            // more features that we need for now. maybe some other time.
            static constexpr uint32_t LINUX_CAPABILITY_VERSION_3 = 0x20080522;
            static constexpr uint32_t LINUX_CAPABILITY_U32S_3 = 2;
            struct user_cap_header_struct
            {
                uint32_t version;
                int pid;
            } hdr = {LINUX_CAPABILITY_VERSION_3, 0};
            struct user_cap_data_struct
            {
                uint32_t effective;
                uint32_t permitted;
                uint32_t inheritable;
            } data[LINUX_CAPABILITY_U32S_3] = {};
            for (auto cap : options.caps) {
                assert(cap / 32 < LINUX_CAPABILITY_U32S_3);
                data[cap / 32].permitted |= 1 << (cap % 32);
                data[cap / 32].inheritable |= 1 << (cap % 32);
            }
            if (syscall(SYS_capset, &hdr, data)) {
                throw SysError("couldn't set capabilities");
            }

            for (auto cap : options.caps) {
                if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, cap, 0, 0) < 0) {
                    throw SysError("couldn't set ambient caps");
                }
            }
        }
#endif

        Strings args_(options.args);
        args_.push_front(options.argv0.value_or(options.program));

        restoreProcessContext();

        if (options.searchPath)
            execvp(options.program.c_str(), stringsToCharPtrs(args_).data());
            // This allows you to refer to a program with a pathname relative
            // to the PATH variable.
        else
            execv(options.program.c_str(), stringsToCharPtrs(args_).data());

        throw SysError("executing '%1%'", options.program);
    }, processOptions)};

    out.writeSide.close();

    return RunningProgram{
        options.program,
        std::move(pid),
        options.captureStdout ? std::move(out.readSide) : AutoCloseFD{}
    };
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
