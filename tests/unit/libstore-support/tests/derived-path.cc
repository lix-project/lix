#include <regex>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <rapidcheck.h>
#pragma GCC diagnostic pop

#include "tests/derived-path.hh"

namespace rc {
using namespace nix;

Gen<DerivedPath::Opaque> Arbitrary<DerivedPath::Opaque>::arbitrary()
{
    return gen::just(DerivedPath::Opaque {
        .path = *gen::arbitrary<StorePath>(),
    });
}

Gen<SingleDerivedPath::Built> Arbitrary<SingleDerivedPath::Built>::arbitrary()
{
    return gen::just(SingleDerivedPath::Built {
        .drvPath = *gen::arbitrary<DerivedPathOpaque>(),
        .output = (*gen::arbitrary<StorePathName>()).name,
    });
}

Gen<DerivedPath::Built> Arbitrary<DerivedPath::Built>::arbitrary()
{
    return gen::just(DerivedPath::Built {
        .drvPath = *gen::arbitrary<DerivedPathOpaque>(),
        .outputs = *gen::arbitrary<OutputsSpec>(),
    });
}

Gen<SingleDerivedPath> Arbitrary<SingleDerivedPath>::arbitrary()
{
    switch (*gen::inRange<uint8_t>(0, std::variant_size_v<SingleDerivedPath::Raw>)) {
    case 0:
        return gen::just<SingleDerivedPath>(*gen::arbitrary<SingleDerivedPath::Opaque>());
    case 1:
        return gen::just<SingleDerivedPath>(*gen::arbitrary<SingleDerivedPath::Built>());
    default:
        assert(false);
    }
}

Gen<DerivedPath> Arbitrary<DerivedPath>::arbitrary()
{
    switch (*gen::inRange<uint8_t>(0, std::variant_size_v<DerivedPath::Raw>)) {
    case 0:
        return gen::just<DerivedPath>(*gen::arbitrary<DerivedPath::Opaque>());
    case 1:
        return gen::just<DerivedPath>(*gen::arbitrary<DerivedPath::Built>());
    default:
        assert(false);
    }
}

}
