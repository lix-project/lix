#include "lix/libexpr/eval-error.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/value.hh"
#include "lix/libutil/types.hh"

namespace nix {

template<std::derived_from<EvalError> T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::withExitStatus(unsigned int exitStatus) &&
{
    error->withExitStatus(exitStatus);
    return std::move(*this);
}

template<std::derived_from<EvalError> T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::atPos(PosIdx pos) &&
{
    error->err.pos = positions[pos];
    return std::move(*this);
}

template<std::derived_from<EvalError> T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::withTrace(PosIdx pos, const std::string_view text) &&
{
    error->err.traces.push_front(
        Trace{.pos = positions[pos], .hint = HintFmt(std::string(text))});
    return std::move(*this);
}

template<std::derived_from<EvalError> T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::withSuggestions(Suggestions & s) &&
{
    error->err.suggestions = s;
    return std::move(*this);
}

template<std::derived_from<EvalError> T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::withFrame(const Env & env, const Expr & expr) &&
{
    if (debug) {
        error->frame = debug->addTrace(DebugTrace{
            .pos = positions[expr.getPos()],
            .expr = expr,
            .env = env,
            .hint = HintFmt("Fake frame for debugging purposes"),
            .isError = true
        }).entry;
    }
    return std::move(*this);
}

template<std::derived_from<EvalError> T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::addTrace(PosIdx pos, HintFmt hint) &&
{
    error->addTrace(positions[pos], hint);
    return std::move(*this);
}

template<std::derived_from<EvalError> T>
template<typename... Args>
EvalErrorBuilder<T>
EvalErrorBuilder<T>::addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs) &&
{

    addTrace(positions[pos], HintFmt(std::string(formatString), formatArgs...));
    return std::move(*this);
}

template<std::derived_from<EvalError> T>
void EvalErrorBuilder<T>::debugThrow(NeverAsync) &&
{
    if (debug) {
        if (auto last = debug->traces().next()) {
            const Env * env = &(*last)->env;
            const Expr * expr = &(*last)->expr;
            debug->onEvalError(error.get(), *env, *expr);
        }
    }

    throw *error; // NOLINT(lix-foreign-exceptions): type dependent
}

template<std::derived_from<EvalError> T>
void EvalErrorBuilder<T>::throw_() &&
{
    throw *error; // NOLINT(lix-foreign-exceptions): type dependent
}

template class EvalErrorBuilder<EvalError>;
template class EvalErrorBuilder<AssertionError>;
template class EvalErrorBuilder<ThrownError>;
template class EvalErrorBuilder<Abort>;
template class EvalErrorBuilder<TypeError>;
template class EvalErrorBuilder<UndefinedVarError>;
template class EvalErrorBuilder<MissingArgumentError>;
template class EvalErrorBuilder<InfiniteRecursionError>;
template class EvalErrorBuilder<InvalidPathError>;

}
