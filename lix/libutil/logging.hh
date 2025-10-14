#pragma once
///@file

#include "lix/libutil/types.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/config.hh"
#include "result.hh"
#include "serialise.hh"
#include <kj/async.h>
#include <optional>

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

template<>
struct json::is_integral_enum<ActivityType> : std::true_type {};

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

template<>
struct json::is_integral_enum<ResultType> : std::true_type {};

typedef uint64_t ActivityId;

struct LoggerSettings : Config
{
    #include "logging-settings.gen.inc"
};

extern LoggerSettings loggerSettings;

class Activity;

class Logger
{
    friend class Activity;

public:

    enum class [[nodiscard]] BufferState {
        HasSpace,
        NeedsFlush,
    };

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

    virtual BufferState bufferState() const
    {
        return BufferState::HasSpace;
    }

    virtual BufferState log(Verbosity lvl, std::string_view s) = 0;

    virtual BufferState logEI(const ErrorInfo & ei) = 0;

    BufferState logEI(Verbosity lvl, ErrorInfo ei)
    {
        ei.level = lvl;
        return logEI(ei);
    }

    Activity startActivity(
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        const Fields & fields = {},
        const Activity * parent = nullptr
    );

    Activity
    startActivity(ActivityType type, const Fields & fields = {}, const Activity * parent = nullptr);

    virtual kj::Promise<Result<void>> flush()
    {
        return {result::success()};
    }

    virtual void waitForSpace() {}

protected:
    virtual BufferState startActivityImpl(
        ActivityId act,
        Verbosity lvl,
        ActivityType type,
        const std::string & s,
        const Fields & fields,
        ActivityId parent
    )
    {
        return BufferState::HasSpace;
    }

    virtual BufferState stopActivityImpl(ActivityId act)
    {
        return BufferState::HasSpace;
    }

    virtual BufferState resultImpl(ActivityId act, ResultType type, const Fields & fields)
    {
        return BufferState::HasSpace;
    }

public:
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

class Activity
{
    Logger * logger;
    ActivityId id;

    explicit Activity(Logger & logger);

public:
    Activity(Activity && other) : logger(nullptr), id(0)
    {
        swap(other);
    }

    Activity & operator=(Activity && other)
    {
        Activity(std::move(other)).swap(*this);
        return *this;
    }

    Activity(const Activity & act) = delete;
    Activity & operator=(const Activity & act) = delete;

    ~Activity();

    Logger & getLogger() const
    {
        return *logger;
    }

    void swap(Activity & other)
    {
        std::swap(logger, other.logger);
        std::swap(id, other.id);
    }

    Activity addChild(
        Verbosity level,
        ActivityType type,
        const std::string & s = "",
        const Logger::Fields & fields = {}
    ) const
    {
        return logger->startActivity(level, type, s, fields, this);
    }

    Logger::BufferState progress(
        uint64_t done = 0, uint64_t expected = 0, uint64_t running = 0, uint64_t failed = 0
    ) const
    {
        return result(resProgress, done, expected, running, failed);
    }

    Logger::BufferState setExpected(ActivityType type2, uint64_t expected) const
    {
        return result(resSetExpected, type2, expected);
    }

    template<typename... Args>
    Logger::BufferState result(ResultType type, const Args &... args) const
    {
        Logger::Fields fields;
        nop{(fields.emplace_back(Logger::Field(args)), 1)...};
        return result(type, fields);
    }

    Logger::BufferState result(ResultType type, const Logger::Fields & fields) const
    {
        return logger->resultImpl(id, type, fields);
    }

    friend class Logger;
};

extern Logger * logger;

Logger * makeSimpleLogger(bool printBuildLogs = true);

Logger * makeJSONLogger(Logger & prevLogger);

/**
 * suppress msgs > this
 */
extern Verbosity verbosity;

#define ACTIVITY_PROGRESS(act, ...)                                                     \
    do {                                                                                \
        auto && _lix_act = (act);                                                       \
        if (_lix_act.progress(__VA_ARGS__) == ::nix::Logger::BufferState::NeedsFlush) { \
            LIX_TRY_AWAIT(_lix_act.getLogger().flush());                                \
        }                                                                               \
    } while (0)
#define ACTIVITY_RESULT(act, ...)                                                     \
    do {                                                                              \
        auto && _lix_act = (act);                                                     \
        if (_lix_act.result(__VA_ARGS__) == ::nix::Logger::BufferState::NeedsFlush) { \
            LIX_TRY_AWAIT(_lix_act.getLogger().flush());                              \
        }                                                                             \
    } while (0)
#define ACTIVITY_SET_EXPECTED(act, ...)                                                    \
    do {                                                                                   \
        auto && _lix_act = (act);                                                          \
        if (_lix_act.setExpected(__VA_ARGS__) == ::nix::Logger::BufferState::NeedsFlush) { \
            LIX_TRY_AWAIT(_lix_act.getLogger().flush());                                   \
        }                                                                                  \
    } while (0)

#define ACTIVITY_PROGRESS_SYNC(aio, act, ...)                                           \
    do {                                                                                \
        auto && _lix_act = (act);                                                       \
        if (_lix_act.progress(__VA_ARGS__) == ::nix::Logger::BufferState::NeedsFlush) { \
            (aio).blockOn(_lix_act.getLogger().flush());                                \
        }                                                                               \
    } while (0)
#define ACTIVITY_RESULT_SYNC(aio, act, ...)                                           \
    do {                                                                              \
        auto && _lix_act = (act);                                                     \
        if (_lix_act.result(__VA_ARGS__) == ::nix::Logger::BufferState::NeedsFlush) { \
            (aio).blockOn(_lix_act.getLogger().flush());                              \
        }                                                                             \
    } while (0)
#define ACTIVITY_SET_EXPECTED_SYNC(aio, act, ...)                                          \
    do {                                                                                   \
        auto && _lix_act = (act);                                                          \
        if (_lix_act.setExpected(__VA_ARGS__) == ::nix::Logger::BufferState::NeedsFlush) { \
            (aio).blockOn(_lix_act.getLogger().flush());                                   \
        }                                                                                  \
    } while (0)

// NOTE: unlike activity progress updates we *do not* flush buffers for "normal"
// messages. the largest producers or log items are builds (which report logs as
// activity results), the curl thread (which can only wait after we have reached
// a buffer watermark, not actually flush it due to kj limitations), debug level
// log messages (which are not latency-sensitive), and interactive use (which we
// only ever run with a logger that writes directly to stderr). we have tried to
// add buffer flushing to these log macros, but it turned out to be *incredibly*
// invasive in many places to outright impossible in some, such as logError in a
// catch block (because c++ doesn't allow awaits in catch blocks). fucking mess.

/**
 * Print a message with the standard ErrorInfo format.
 * In general, use these 'log' macros for reporting problems that may require user
 * intervention or that need more explanation.  Use the 'print' macros for more
 * lightweight status messages.
 */
#define logErrorInfo(level, errorInfo...)                    \
    do {                                                     \
        if ((level) <= ::nix::verbosity) {                   \
            (void) ::nix::logger->logEI((level), errorInfo); \
        }                                                    \
    } while (0)

#define logError(errorInfo...) logErrorInfo(::nix::lvlError, errorInfo)
#define logWarning(errorInfo...) logErrorInfo(::nix::lvlWarn, errorInfo)

/**
 * Print a string message if the current log level is at least the specified
 * level. Note that this has to be implemented as a macro to ensure that the
 * arguments are evaluated lazily. The format string *must* be a literal.
 */
#define printMsgUsing(loggerParam, level, fs, args...)                                            \
    do {                                                                                          \
        auto _lix_logger_print_lvl = level;                                                       \
        const char * _lix_format = []<size_t N>(const char(&_lix_fs)[N]) { return _lix_fs; }(fs); \
        if (_lix_logger_print_lvl <= ::nix::verbosity) {                                          \
            (void                                                                                 \
            ) loggerParam->log(_lix_logger_print_lvl, ::nix::HintFmt(_lix_format, ##args).str()); \
        }                                                                                         \
    } while (0)
#define printMsg(level, fs, args...) printMsgUsing(::nix::logger, level, fs, ##args)

#define printWarning(fs, args...) printMsg(::nix::lvlWarn, fs, ##args)
#define printError(fs, args...) printMsg(::nix::lvlError, fs, ##args)
#define notice(fs, args...) printMsg(::nix::lvlNotice, fs, ##args)
#define printInfo(fs, args...) printMsg(::nix::lvlInfo, fs, ##args)
#define printTalkative(fs, args...) printMsg(::nix::lvlTalkative, fs, ##args)
#define debug(fs, args...) printMsg(::nix::lvlDebug, fs, ##args)
#define vomit(fs, args...) printMsg(::nix::lvlVomit, fs, ##args)

#define printTaggedWarning(fs, args...) \
    printWarning(ANSI_WARNING "warning:" ANSI_NORMAL " " fs, ##args)

void writeLogsToStderr(std::string_view s);

/** Logs a fatal message as loudly as possible. This will go into syslog as well as stderr.
 * The purpose of this function is making failures with redirected stderr louder. */
void logFatal(std::string const & s);

/**
 * @param source A noun phrase describing the source of the message, e.g. "the builder".
 */
std::optional<JSON> parseJSONMessage(const std::string & msg, std::string_view source);

/**
 * @param source A noun phrase describing the source of the message, e.g. "the builder".
 */
[[nodiscard]]
std::optional<Logger::BufferState> handleJSONLogMessage(
    JSON & json,
    const Activity & act,
    std::map<ActivityId, Activity> & activities,
    std::string_view source
);

/**
 * @param source A noun phrase describing the source of the message, e.g. "the builder".
 */
[[nodiscard]]
std::optional<Logger::BufferState> handleJSONLogMessage(
    const std::string & msg,
    const Activity & act,
    std::map<ActivityId, Activity> & activities,
    std::string_view source
);

/**
 * Split a log stream into lines, processing carriage returns (`\r`) as a terminal would.
 */
class LogLineSplitter
{
    std::string line;
    size_t pos = 0;

public:
    /**
     * Feeds some input to the splitter and returns the first full line or `nullopt` if
     * there is no complete line in the buffer yet. If any input remains `input` is set
     * to the unconsumed data and `feed` should be called again until `input` is empty.
     * If this function returns `nullopt` it guarantees that `input` is fully consumed.
     */
    std::optional<std::string> feed(std::string_view & input);

    /**
     * Clear the line buffer and return its current contents.
     */
    std::string finish();
};
}
