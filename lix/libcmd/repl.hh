#pragma once
///@file

#include "lix/libexpr/eval.hh"

namespace nix {

struct AbstractNixRepl
{
    typedef std::vector<std::pair<Value*,std::string>> AnnotatedValues;

    static ReplExitStatus
    run(const SearchPath & searchPath,
        nix::ref<Store> store,
        ref<EvalState> state,
        std::function<AnnotatedValues()> getValues,
        const ValMap & extraEnv,
        Bindings * autoArgs);

    static ReplExitStatus runSimple(
        ref<EvalState> evalState,
        const ValMap & extraEnv);

protected:
    ref<EvalState> state;
    Bindings * autoArgs;

    AbstractNixRepl(ref<EvalState> state)
        : state(state)
    { }

    virtual void initEnv() = 0;

    virtual ReplExitStatus mainLoop() = 0;
};

}
