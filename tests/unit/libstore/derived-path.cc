#include <regex>

#include <gtest/gtest.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <rapidcheck/gtest.h>
#pragma GCC diagnostic pop

#include "tests/derived-path.hh"
#include "tests/libstore.hh"

namespace nix {

class DerivedPathTest : public LibStoreTest
{
};

#ifndef COVERAGE

RC_GTEST_FIXTURE_PROP(
    DerivedPathTest,
    prop_legacy_round_rip,
    (const DerivedPath & o))
{
    RC_ASSERT(o == DerivedPath::parseLegacy(*store, o.to_string_legacy(*store)));
}

#endif

}
