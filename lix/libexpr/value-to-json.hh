#pragma once
///@file

#include "lix/libexpr/nixexpr.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libutil/json-fwd.hh"

namespace nix {

JSON printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore = true);

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, std::ostream & str, NixStringContext & context, bool copyToStore = true);

}
