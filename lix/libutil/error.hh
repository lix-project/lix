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

#include "lix/libutil/json-fwd.hh"
#include "lix/libutil/suggestions.hh"
#include "lix/libutil/fmt.hh"

#include <cstring>
#include <exception>
#include <list>
#include <memory>
#include <optional>

#include <source_location>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <system_error>
#include <type_traits>

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

template<>
struct json::is_integral_enum<Verbosity> : std::true_type {};

Verbosity verbosityFromIntClamped(int val);

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
    Verbosity level = Verbosity::lvlError;
    HintFmt msg;
    std::shared_ptr<Pos> pos;
    std::list<Trace> traces = {};

    /**
     * Exit status.
     */
    unsigned int status = 1;

    Suggestions suggestions = {};
};

std::ostream & showErrorInfo(std::ostream & out, const ErrorInfo & einfo, bool showTrace);

/**
 * Base class for both errors we can handle (c.f. `BaseError`) and anything
 * we want to log and terminate when encountered (c.f. `ForeignException`).
 */
class BaseException : public std::exception
{
public:
    struct AsyncTraceFrame
    {
        std::source_location location;
        std::optional<std::string> description;
    };

private:
    /**
     * Approximate list of async tasks this exception propagated through.
     * It is the responsibility of each task to add itself to the back of
     * this list during stack unwinding. `TRY_AWAIT` does this when used.
     */
    std::shared_ptr<std::list<AsyncTraceFrame>> _asyncTrace;

public:
    std::shared_ptr<const std::list<AsyncTraceFrame>> asyncTrace() const
    {
        return _asyncTrace;
    }

    void
    addAsyncTrace(std::source_location loc, std::optional<std::string> description = std::nullopt)
    {
        if (!_asyncTrace) {
            _asyncTrace = std::make_shared<std::list<AsyncTraceFrame>>();
        }
        _asyncTrace->push_back(AsyncTraceFrame{loc, std::move(description)});
    }
};

/**
 * BaseError should generally not be caught, as it has Interrupted as
 * a subclass. Catch Error instead.
 */
class BaseError : public BaseException
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
    /* NOLINTNEXTLINE(bugprone-macro-parentheses) */    \
    class newClass : public superClass                  \
    {                                                   \
    public:                                             \
        using superClass::superClass;                   \
    }

MakeError(Error, BaseError);
MakeError(UsageError, Error);
MakeError(UnimplementedError, Error);

/**
 * Wrap any exception as BaseException. We don't wrap as Error because we do not
 * usually want to catch these exceptions and we don't use std::nested_exception
 * because we need the dynamic type to preserve the original exception for that.
 * This should never be used to wrap something that already is a BaseError (or a
 * BaseException), but this isn't checked since this should not be thrown often.
 */
class ForeignException : public BaseException
{
    std::shared_ptr<std::string> _what;

    ForeignException(
        std::string && what, std::exception_ptr && inner, const std::type_info & innerType
    )
        : _what(std::make_shared<std::string>(std::move(what)))
        , inner(std::move(inner)) // NOLINT(bugprone-throw-keyword-missing): this stores exceptions.
        , innerType(typeid(inner))
    {
    }

public:
    const std::exception_ptr inner;
    const std::type_info & innerType;

    struct Unknown
    {};

    static ForeignException wrapCurrent()
    {
        try {
            std::rethrow_exception(std::current_exception());
            // NOLINTNEXTLINE(lix-foreign-exceptions): this is foreign exception support code.
        } catch (std::exception & e) {
            return {e.what(), std::current_exception(), typeid(e)};
        } catch (...) {
            return {"(non-std::exception)", std::current_exception(), typeid(Unknown)};
        }
    }

    [[noreturn]]
    void rethrow() const
    {
        std::rethrow_exception(inner);
    }

    template<typename E>
    E * as() const
    {
        try {
            rethrow();
        } catch (E & e) { // NOLINT(lix-foreign-exceptions)
            return &e;
        } catch (...) {
            return nullptr;
        }
    }

    template<typename E>
    bool is() const
    {
        return as<E>() != nullptr;
    }

    const char * what() const noexcept override
    {
        return _what->c_str();
    }
};

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
    SysError(std::error_code ec, const Args & ... args)
        : Error("")
    {
        errNo = ec.value();
        auto hf = HintFmt(args...);
        err.msg = HintFmt("%1%: %2%", Uncolored(hf.str()), ec.message());
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
 *
 * If you're not in a destructor, you usually want to use `ignoreExceptionExceptInterrupt()`.
 *
 * This function might also be used in callbacks whose caller may not handle exceptions,
 * but ideally we propagate the exception using an exception_ptr in such cases.
 * See e.g. `PackBuilderContext`
 */
void ignoreExceptionInDestructor(Verbosity lvl = lvlError);

/**
 * Not destructor-safe.
 * Print an error message, then ignore the exception.
 * If the exception is an `Interrupted` exception, rethrow it.
 *
 * This may be used in a few places where Interrupt can't happen, but that's ok.
 */
void ignoreExceptionExceptInterrupt(Verbosity lvl = lvlError);

/** Print out details about an exception and its stack trace. */
void logException(std::string_view message_prefix, const std::exception & ex);
}
