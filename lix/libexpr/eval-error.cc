#include "lix/libexpr/eval-error.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/value.hh"

namespace nix {

template<class T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::withExitStatus(unsigned int exitStatus) &&
{
    error->withExitStatus(exitStatus);
    return std::move(*this);
}

template<class T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::atPos(PosIdx pos) &&
{
    error->err.pos = state.positions[pos];
    return std::move(*this);
}

template<class T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::atPos(Value & value, PosIdx fallback) &&
{
    return std::move(*this).atPos(value.determinePos(fallback));
}

template<class T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::withTrace(PosIdx pos, const std::string_view text) &&
{
    error->err.traces.push_front(
        Trace{.pos = state.positions[pos], .hint = HintFmt(std::string(text))});
    return std::move(*this);
}

template<class T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::withSuggestions(Suggestions & s) &&
{
    error->err.suggestions = s;
    return std::move(*this);
}

template<class T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::withFrame(const Env & env, const Expr & expr) &&
{
    if (state.debug) {
        error->frame = state.debug->addTrace(DebugTrace{
            .pos = state.positions[expr.getPos()],
            .expr = expr,
            .env = env,
            .hint = HintFmt("Fake frame for debugging purposes"),
            .isError = true
        }).entry;
    }
    return std::move(*this);
}

template<class T>
EvalErrorBuilder<T> EvalErrorBuilder<T>::addTrace(PosIdx pos, HintFmt hint) &&
{
    error->addTrace(state.positions[pos], hint);
    return std::move(*this);
}

template<class T>
template<typename... Args>
EvalErrorBuilder<T>
EvalErrorBuilder<T>::addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs) &&
{

    addTrace(state.positions[pos], HintFmt(std::string(formatString), formatArgs...));
    return std::move(*this);
}

template<class T>
void EvalErrorBuilder<T>::debugThrow() &&
{
    if (state.debug) {
        if (auto last = state.debug->traces().next()) {
            const Env * env = &(*last)->env;
            const Expr * expr = &(*last)->expr;
            state.debug->onEvalError(error.get(), *env, *expr);
        }
    }

    throw *error;
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
