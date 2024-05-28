#pragma once
/**
 * @file
 *
 * @brief This file defines two main structs/classes used in nix error handling.
 *
 * ErrorInfo provides a standard payload of error information, with conversion to string
 * happening in the logger rather than at the call site.
 *
 * BaseError is the ancestor of nix specific exceptions (and Interrupted), and contains
 * an ErrorInfo.
 *
 * ErrorInfo structs are sent to the logger as part of an exception, or directly with the
 * logError or logWarning macros.
 * See libutil/tests/logging.cc for usage examples.
 */

#include "suggestions.hh"
#include "ref.hh"
#include "types.hh"
#include "fmt.hh"

#include <cstring>
#include <list>
#include <memory>
#include <map>
#include <optional>
#include <compare>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {


typedef enum {
    lvlError = 0,
    lvlWarn,
    lvlNotice,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;

/**
 * The lines of code surrounding an error.
 */
struct LinesOfCode {
    std::optional<std::string> prevLineOfCode;
    std::optional<std::string> errLineOfCode;
    std::optional<std::string> nextLineOfCode;
};

struct Pos;

void printCodeLines(std::ostream & out,
    const std::string & prefix,
    const Pos & errPos,
    const LinesOfCode & loc);

struct Trace {
    std::shared_ptr<Pos> pos;
    HintFmt hint;
};

inline bool operator<(const Trace& lhs, const Trace& rhs);
inline bool operator> (const Trace& lhs, const Trace& rhs);
inline bool operator<=(const Trace& lhs, const Trace& rhs);
inline bool operator>=(const Trace& lhs, const Trace& rhs);

struct ErrorInfo {
    Verbosity level;
    HintFmt msg;
    std::shared_ptr<Pos> pos;
    std::list<Trace> traces;

    /**
     * Exit status.
     */
    unsigned int status = 1;

    Suggestions suggestions;

    static std::optional<std::string> programName;
};

std::ostream & showErrorInfo(std::ostream & out, const ErrorInfo & einfo, bool showTrace);

/**
 * BaseError should generally not be caught, as it has Interrupted as
 * a subclass. Catch Error instead.
 */
class BaseError : public std::exception
{
protected:
    mutable ErrorInfo err;

    /**
     * Cached formatted contents of `err.msg`.
     */
    mutable std::optional<std::string> what_;
    /**
     * Format `err.msg` and set `what_` to the resulting value.
     */
    const std::string & calcWhat() const;

public:
    BaseError(const BaseError &) = default;

    BaseError & operator=(BaseError const & rhs) = default;

    template<typename... Args>
    BaseError(unsigned int status, const Args & ... args)
        : err { .level = lvlError, .msg = HintFmt(args...), .status = status }
    { }

    template<typename... Args>
    explicit BaseError(const std::string & fs, const Args & ... args)
        : err { .level = lvlError, .msg = HintFmt(fs, args...) }
    { }

    template<typename... Args>
    BaseError(const Suggestions & sug, const Args & ... args)
        : err { .level = lvlError, .msg = HintFmt(args...), .suggestions = sug }
    { }

    BaseError(HintFmt hint)
        : err { .level = lvlError, .msg = hint }
    { }

    BaseError(ErrorInfo && e)
        : err(std::move(e))
    { }

    BaseError(const ErrorInfo & e)
        : err(e)
    { }

    const char * what() const noexcept override { return calcWhat().c_str(); }
    const std::string & msg() const { return calcWhat(); }
    const ErrorInfo & info() const { calcWhat(); return err; }

    void withExitStatus(unsigned int status)
    {
        err.status = status;
    }

    void atPos(std::shared_ptr<Pos> pos) {
        err.pos = pos;
    }

    void pushTrace(Trace trace)
    {
        err.traces.push_front(trace);
    }

    template<typename... Args>
    void addTrace(std::shared_ptr<Pos> && e, std::string_view fs, const Args & ... args)
    {
        addTrace(std::move(e), HintFmt(std::string(fs), args...));
    }

    void addTrace(std::shared_ptr<Pos> && e, HintFmt hint);

    bool hasTrace() const { return !err.traces.empty(); }

    const ErrorInfo & info() { return err; };
};

#define MakeError(newClass, superClass) \
    class newClass : public superClass                  \
    {                                                   \
    public:                                             \
        using superClass::superClass;                   \
    }

MakeError(Error, BaseError);
MakeError(UsageError, Error);
MakeError(UnimplementedError, Error);

class SysError : public Error
{
public:
    int errNo;

    template<typename... Args>
    SysError(int errNo_, const Args & ... args)
        : Error("")
    {
        errNo = errNo_;
        auto hf = HintFmt(args...);
        err.msg = HintFmt("%1%: %2%", Uncolored(hf.str()), strerror(errNo));
    }

    template<typename... Args>
    SysError(const Args & ... args)
        : SysError(errno, args ...)
    {
    }
};

/**
 * Exception handling in destructors: print an error message, then
 * ignore the exception.
 */
void ignoreException(Verbosity lvl = lvlError);

}
