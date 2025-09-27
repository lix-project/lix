#pragma once
///@file

#include "lix/libexpr/eval.hh"
#include "lix/libutil/types.hh"

namespace nix {

struct AbstractNixRepl : NeverAsync
{
    typedef std::vector<std::pair<Value, std::string>> AnnotatedValues;

    static ReplExitStatus
    run(const SearchPath & searchPath,
        nix::ref<Store> store,
        EvalState & state,
        std::function<AnnotatedValues()> getValues,
        const ValMap & extraEnv,
        Bindings * autoArgs);

    static ReplExitStatus runSimple(
        EvalState & evalState,
        const ValMap & extraEnv);

protected:
    EvalState & state;
    Bindings * autoArgs;

    AbstractNixRepl(EvalState & state)
        : state(state)
    { }

    virtual ~AbstractNixRepl()
    { }

    virtual void initEnv() = 0;

    virtual ReplExitStatus mainLoop() = 0;
};

}
