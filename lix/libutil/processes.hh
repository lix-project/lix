#pragma once
///@file

#include "lix/libutil/types.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <functional>
#include <map>
#include <optional>

namespace nix {

struct Sink;
struct Source;

class Pid
{
    pid_t pid = -1;
    bool separatePG = false;
    int killSignal = SIGKILL;
public:
    Pid();
    explicit Pid(pid_t pid): pid(pid) {}
    Pid(Pid && other);
    Pid & operator=(Pid && other);
    ~Pid() noexcept(false);
    explicit operator bool() const { return pid != -1; }
    int kill();
    int wait();

    void setSeparatePG(bool separatePG);
    void setKillSignal(int signal);
    pid_t release();
    pid_t get() const { return pid; }
};

/**
 * Kill all processes running under the specified uid by sending them
 * a SIGKILL.
 */
void killUser(uid_t uid);


/**
 * Fork a process that runs the given function, and return the child
 * pid to the caller.
 */
struct ProcessOptions
{
    bool dieWithParent = true;
    /**
     * use clone() with the specified flags (Linux only)
     */
    int cloneFlags = 0;
};

[[nodiscard]]
Pid startProcess(std::function<void()> fun, const ProcessOptions & options = ProcessOptions());


/**
 * Run a program and return its stdout in a string (i.e., like the
 * shell backtick operator).
 */
std::string runProgram(Path program, bool searchPath = false,
    const Strings & args = Strings(), bool isInteractive = false);

struct RunOptions
{
    struct Redirection
    {
        int dup, from;
    };

    Path program;
    bool searchPath = true;
    std::optional<std::string> argv0;
    Strings args = {};
    std::optional<uid_t> uid = {};
    std::optional<uid_t> gid = {};
    std::optional<Path> chdir = {};
    std::optional<std::map<std::string, std::string>> environment = {};
    bool captureStdout = false;
    bool isInteractive = false;
    std::vector<Redirection> redirections;
#if __linux__
    std::set<long> caps;
#endif
};

struct [[nodiscard("you must call RunningProgram::wait()")]] RunningProgram
{
    friend RunningProgram runProgram2(const RunOptions & options);

private:
    Path program;
    Pid pid;
    std::unique_ptr<Source> stdoutSource;
    AutoCloseFD stdout_;

    RunningProgram(PathView program, Pid pid, AutoCloseFD stdout);

public:
    RunningProgram() = default;
    RunningProgram(RunningProgram &&) = default;
    RunningProgram & operator=(RunningProgram &&) = default;
    ~RunningProgram();

    explicit operator bool() const { return bool(pid); }

    std::tuple<pid_t, std::unique_ptr<Source>, int> release();

    int kill();
    [[nodiscard]]
    int wait();
    void waitAndCheck();

    std::optional<int> getStdoutFD() const
    {
        return stdout_ ? std::optional(stdout_.get()) : std::nullopt;
    }

    Source * getStdout() const { return stdoutSource.get(); };
};

std::pair<int, std::string> runProgram(RunOptions && options);

RunningProgram runProgram2(const RunOptions & options);

class ExecError : public Error
{
public:
    int status;

    template<typename... Args>
    ExecError(int status, const Args & ... args)
        : Error(args...), status(status)
    { }
};

/**
 * Convert the exit status of a child as returned by wait() into an
 * error string.
 */
std::string statusToString(int status);

bool statusOk(int status);

}
