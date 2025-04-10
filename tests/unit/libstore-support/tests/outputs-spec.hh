#pragma once
///@file

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <rapidcheck/gen/Arbitrary.h>
#pragma GCC diagnostic pop

#include "lix/libstore/outputs-spec.hh"

#include "tests/path.hh"

namespace rc {
using namespace nix;

template<>
struct Arbitrary<OutputsSpec> {
    static Gen<OutputsSpec> arbitrary();
};

}
