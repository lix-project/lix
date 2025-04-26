#pragma once
///@file

#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/types.hh"
#include "lix/libexpr/pos-idx.hh"
#include "lix/libexpr/pos-table.hh"
#include <concepts>

namespace nix {

struct DebugState;
struct DebugTrace;
struct Env;
struct Expr;
struct Value;

class EvalError;
class EvalState;
template<std::derived_from<EvalError> T>
class EvalErrorBuilder;

class EvalError : public Error
{
    template<std::derived_from<EvalError> T>
    friend class EvalErrorBuilder;

    std::shared_ptr<const DebugTrace> frame;

public:
    using Error::Error;
};

MakeError(ParseError, Error);
MakeError(AssertionError, EvalError);
MakeError(ThrownError, AssertionError);
MakeError(Abort, EvalError);
MakeError(TypeError, EvalError);
MakeError(UndefinedVarError, EvalError);
MakeError(MissingArgumentError, EvalError);
MakeError(RestrictedPathError, Error);
MakeError(InfiniteRecursionError, EvalError);

/**
 * Represents an exception due to an invalid path; that is, it does not exist.
 * It corresponds to `!Store::validPath()`.
 */
struct InvalidPathError : public EvalError
{
public:
    Path path;
    InvalidPathError(const Path & path)
        : EvalError("path '%s' did not exist in the store during evaluation", path)
    {
    }
};

template<std::derived_from<EvalError> T>
class [[nodiscard]] EvalErrorBuilder final
{
    const PosTable & positions;
    DebugState * debug;
    box_ptr<T> error;

public:
    template<typename... Args>
    explicit EvalErrorBuilder(const PosTable & positions, DebugState * debug, const Args &... args)
        : positions(positions)
        , debug{debug}
        , error(make_box_ptr<T>(args...))
    {
    }

    [[gnu::noinline]] EvalErrorBuilder<T> withExitStatus(unsigned int exitStatus) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> atPos(PosIdx pos) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> withTrace(PosIdx pos, const std::string_view text) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> withSuggestions(Suggestions & s) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> withFrame(const Env & e, const Expr & ex) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> addTrace(PosIdx pos, HintFmt hint) &&;

    template<typename... Args>
    [[gnu::noinline]] EvalErrorBuilder<T>
    addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs) &&;

    /**
     * Throw the underlying exception, invoking the debug state callback.
     */
    [[gnu::noinline, gnu::noreturn]] void debugThrow(NeverAsync = {}) &&;

    /**
     * Throw the underlying exception, bypassing the debug state callback.
     */
    [[gnu::noinline, gnu::noreturn]] void throw_() &&;
};

}
