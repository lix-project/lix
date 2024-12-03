#pragma once
///@file

#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/types.hh"
#include "lix/libexpr/pos-idx.hh"

namespace nix {

struct DebugTrace;
struct Env;
struct Expr;
struct Value;

class EvalState;
template<class T>
class EvalErrorBuilder;

class EvalError : public Error
{
    template<class T>
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

template<class T>
class [[nodiscard]] EvalErrorBuilder final
{
    friend class EvalState;

    EvalState & state;

    template<typename... Args>
    explicit EvalErrorBuilder(EvalState & state, const Args &... args)
        : state(state), error(make_box_ptr<T>(args...))
    {
    }

public:
    box_ptr<T> error;

    [[gnu::noinline]] EvalErrorBuilder<T> withExitStatus(unsigned int exitStatus) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> atPos(PosIdx pos) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> atPos(Value & value, PosIdx fallback = noPos) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> withTrace(PosIdx pos, const std::string_view text) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> withSuggestions(Suggestions & s) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> withFrame(const Env & e, const Expr & ex) &&;

    [[gnu::noinline]] EvalErrorBuilder<T> addTrace(PosIdx pos, HintFmt hint) &&;

    template<typename... Args>
    [[gnu::noinline]] EvalErrorBuilder<T>
    addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs) &&;

    /**
     * Throw the underlying exception.
     */
    [[gnu::noinline, gnu::noreturn]] void debugThrow() &&;
};

}
