#pragma once
///@file

#include "lix/libexpr/get-drvs.hh"

namespace nix {

bool createUserEnv(EvalState & state, DrvInfos & elems,
    const Path & profile, bool keepDerivations,
    const std::string & lockToken);

}
