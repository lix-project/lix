#pragma once
///@file

#include "lix/libutil/error.hh"

#include <string_view>

namespace nix {

class EvalState;
struct Value;

MakeError(JSONParseError, Error);

void parseJSON(EvalState & state, const std::string_view & s, Value & v);

}
