#include <gtest/gtest.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <rapidcheck/gtest.h>
#pragma GCC diagnostic pop

#include "tests/derived-path.hh"
#include "tests/libexpr.hh"

namespace nix {

// Testing of trivial expressions
class DerivedPathExpressionTest : public LibExprTest {};

// FIXME: `RC_GTEST_FIXTURE_PROP` isn't calling `SetUpTestSuite` because it is
// no a real fixture.
//
// See https://github.com/emil-e/rapidcheck/blob/master/doc/gtest.md#rc_gtest_fixture_propfixture-name-args
TEST_F(DerivedPathExpressionTest, force_init)
{
}

#ifndef COVERAGE

RC_GTEST_FIXTURE_PROP(
    DerivedPathExpressionTest,
    prop_opaque_path_round_trip,
    (const SingleDerivedPath::Opaque & o))
{
    Value v;
    evaluator.paths.mkStorePathString(o.path, v);
    auto d = state.coerceToSingleDerivedPath(noPos, v, "");
    RC_ASSERT(SingleDerivedPath { o } == d);
}

// TODO use DerivedPath::Built for parameter once it supports a single output
// path only.

RC_GTEST_FIXTURE_PROP(
    DerivedPathExpressionTest,
    prop_derived_path_built_out_path_round_trip,
    (const SingleDerivedPath::Built & b, const StorePath & outPath))
{
    Value v;
    state.mkOutputString(v, b, outPath);
    auto [d, _] = state.coerceToSingleDerivedPathUnchecked(noPos, v, "");
    RC_ASSERT(SingleDerivedPath { b } == d);
}

#endif

} /* namespace nix */
