#pragma once
///@file

#include "types.hh"
#include "error.hh"
#include "config.hh"

namespace nix {

typedef enum {
    actUnknown = 0,
    actCopyPath = 100,
    actFileTransfer = 101,
    actRealise = 102,
    actCopyPaths = 103,
    actBuilds = 104,

    /** Fields:
     * 0: string: path to store derivation being built.
     * 1: string: representing the machine this is being built on. Empty string if local machine.
     * 2: int: curRound, not used anymore, always 1?
     * 3: int: nrRounds, not used anymore always 1?
     */
    actBuild = 105,
    actOptimiseStore = 106,
    actVerifyPaths = 107,

    /** Fields:
     * 0: string: store path
     * 1: string: substituter
     */
    actSubstitute = 108,

    /** Fields:
     * 0: string: store path
     * 1: string: substituter
     */
    actQueryPathInfo = 109,

    /** Fields:
     * 0: string: store path
     */
    actPostBuildHook = 110,
    actBuildWaiting = 111,
} ActivityType;

typedef enum {
    /** Fields:
     * 0: int: bytes linked
     */
    resFileLinked = 100,

    /** Fields:
     * 0: string: last line
     */
    resBuildLogLine = 101,
    resUntrustedPath = 102,
    resCorruptedPath = 103,

    /** Fields:
     * 0: string: phase name
     */
    resSetPhase = 104,

    /** Fields:
     * 0: int: done
     * 1: int: expected
     * 2: int: running
     * 3: int: failed
     */
    resProgress = 105,

    /** Fields:
     * 0: int: ActivityType
     * 1: int: expected
     */
    resSetExpected = 106,

    /** Fields:
     * 0: string: last line
     */
    resPostBuildLogLine = 107,
} ResultType;

typedef uint64_t ActivityId;

struct LoggerSettings : Config
{
    Setting<bool> showTrace{
        this, false, "show-trace",
        R"(
          Whether Lix should print out a stack trace in case of Nix
          expression evaluation errors.
        )"};
};

extern LoggerSettings loggerSettings;

class Logger
{
    friend struct Activity;

public:

    struct Field
    {
        // FIXME: use std::variant.
        enum { tInt = 0, tString = 1 } type;
        uint64_t i = 0;
        std::string s;
        Field(const std::string & s) : type(tString), s(s) { }
        Field(const char * s) : type(tString), s(s) { }
        Field(const uint64_t & i) : type(tInt), i(i) { }
    };

    typedef std::vector<Field> Fields;

    virtual ~Logger() { }

    virtual void pause() { };
    virtual void resetProgress() { };
    virtual void resume() { };

    // Whether the logger prints the whole build log
    virtual bool isVerbose() { return false; }

    virtual void log(Verbosity lvl, std::string_view s) = 0;

    void log(std::string_view s)
    {
        log(lvlInfo, s);
    }

    virtual void logEI(const ErrorInfo & ei) = 0;

    void logEI(Verbosity lvl, ErrorInfo ei)
    {
        ei.level = lvl;
        logEI(ei);
    }

    virtual void warn(const std::string & msg);

    virtual void startActivity(ActivityId act, Verbosity lvl, ActivityType type,
        const std::string & s, const Fields & fields, ActivityId parent) { };

    virtual void stopActivity(ActivityId act) { };

    virtual void result(ActivityId act, ResultType type, const Fields & fields) { };

    virtual void writeToStdout(std::string_view s);

    template<typename... Args>
    inline void cout(const Args & ... args)
    {
        writeToStdout(fmt(args...));
    }

    virtual std::optional<char> ask(std::string_view s)
    { return {}; }

    virtual void setPrintBuildLogs(bool printBuildLogs)
    { }

    virtual void setPrintMultiline(bool printMultiline)
    { }
};

/**
 * A variadic template that does nothing.
 *
 * Useful to call a function with each argument in a parameter pack.
 */
struct nop
{
    template<typename... T> nop(T...)
    { }
};

ActivityId getCurActivity();
void setCurActivity(const ActivityId activityId);

struct Activity
{
    Logger & logger;

    const ActivityId id;

    Activity(Logger & logger, Verbosity lvl, ActivityType type, const std::string & s = "",
        const Logger::Fields & fields = {}, ActivityId parent = getCurActivity());

    Activity(Logger & logger, ActivityType type,
        const Logger::Fields & fields = {}, ActivityId parent = getCurActivity())
        : Activity(logger, lvlError, type, "", fields, parent) { };

    Activity(const Activity & act) = delete;

    ~Activity();

    void progress(uint64_t done = 0, uint64_t expected = 0, uint64_t running = 0, uint64_t failed = 0) const
    { result(resProgress, done, expected, running, failed); }

    void setExpected(ActivityType type2, uint64_t expected) const
    { result(resSetExpected, type2, expected); }

    template<typename... Args>
    void result(ResultType type, const Args & ... args) const
    {
        Logger::Fields fields;
        nop{(fields.emplace_back(Logger::Field(args)), 1)...};
        result(type, fields);
    }

    void result(ResultType type, const Logger::Fields & fields) const
    {
        logger.result(id, type, fields);
    }

    friend class Logger;
};

struct PushActivity
{
    const ActivityId prevAct;
    PushActivity(ActivityId act) : prevAct(getCurActivity()) { setCurActivity(act); }
    ~PushActivity() { setCurActivity(prevAct); }
};

extern Logger * logger;

Logger * makeSimpleLogger(bool printBuildLogs = true);

Logger * makeJSONLogger(Logger & prevLogger);

/**
 * suppress msgs > this
 */
extern Verbosity verbosity;

/**
 * Print a message with the standard ErrorInfo format.
 * In general, use these 'log' macros for reporting problems that may require user
 * intervention or that need more explanation.  Use the 'print' macros for more
 * lightweight status messages.
 */
#define logErrorInfo(level, errorInfo...) \
    do { \
        if ((level) <= nix::verbosity) {     \
            logger->logEI((level), errorInfo);  \
        } \
    } while (0)

#define logError(errorInfo...) logErrorInfo(lvlError, errorInfo)
#define logWarning(errorInfo...) logErrorInfo(lvlWarn, errorInfo)

/**
 * Print a string message if the current log level is at least the specified
 * level. Note that this has to be implemented as a macro to ensure that the
 * arguments are evaluated lazily.
 */
#define printMsgUsing(loggerParam, level, args...) \
    do { \
        auto __lvl = level; \
        if (__lvl <= nix::verbosity) { \
            loggerParam->log(__lvl, fmt(args)); \
        } \
    } while (0)
#define printMsg(level, args...) printMsgUsing(logger, level, args)

#define printError(args...) printMsg(lvlError, args)
#define notice(args...) printMsg(lvlNotice, args)
#define printInfo(args...) printMsg(lvlInfo, args)
#define printTalkative(args...) printMsg(lvlTalkative, args)
#define debug(args...) printMsg(lvlDebug, args)
#define vomit(args...) printMsg(lvlVomit, args)

/**
 * if verbosity >= lvlWarn, print a message with a yellow 'warning:' prefix.
 */
template<typename... Args>
inline void warn(const std::string & fs, const Args & ... args)
{
    logger->warn(HintFmt(fs, args...).str());
}

#define warnOnce(haveWarned, args...) \
    if (!haveWarned) {                \
        haveWarned = true;            \
        warn(args);                   \
    }

void writeToStderr(std::string_view s);

}
