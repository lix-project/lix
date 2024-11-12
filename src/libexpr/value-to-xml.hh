#pragma once
///@file

#include "lix/libexpr/nixexpr.hh"
#include "lix/libexpr/eval.hh"

#include <string>
#include <map>

namespace nix {

void printValueAsXML(EvalState & state, bool strict, bool location,
    Value & v, std::ostream & out, NixStringContext & context, const PosIdx pos);

}
