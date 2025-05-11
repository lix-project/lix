#include <gtest/gtest.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <rapidcheck/gtest.h>
#pragma GCC diagnostic pop

#include "tests/path.hh"
#include "tests/libexpr.hh"
#include "tests/value/context.hh"

namespace nix {

// Test a few cases of invalid string context elements.

TEST(NixStringContextElemTest, empty_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse(""),
        BadNixStringContextElem);
}

TEST(NixStringContextElemTest, single_bang_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse("!"),
        BadNixStringContextElem);
}

TEST(NixStringContextElemTest, double_bang_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse("!!/"),
        BadStorePath);
}

TEST(NixStringContextElemTest, eq_slash_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse("=/"),
        BadStorePath);
}

TEST(NixStringContextElemTest, slash_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse("/"),
        BadStorePath);
}

/**
 * Round trip (string <-> data structure) test for
 * `NixStringContextElem::Opaque`.
 */
TEST(NixStringContextElemTest, opaque) {
    std::string_view opaque = "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x";
    auto elem = NixStringContextElem::parse(opaque);
    auto * p = std::get_if<NixStringContextElem::Opaque>(&elem.raw);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->path, StorePath { opaque });
    ASSERT_EQ(elem.to_string(), opaque);
}

/**
 * Round trip (string <-> data structure) test for
 * `NixStringContextElem::DrvDeep`.
 */
TEST(NixStringContextElemTest, drvDeep) {
    std::string_view drvDeep = "=g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv";
    auto elem = NixStringContextElem::parse(drvDeep);
    auto * p = std::get_if<NixStringContextElem::DrvDeep>(&elem.raw);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->drvPath, StorePath { drvDeep.substr(1) });
    ASSERT_EQ(elem.to_string(), drvDeep);
}

/**
 * Round trip (string <-> data structure) test for a simpler
 * `NixStringContextElem::Built`.
 */
TEST(NixStringContextElemTest, built_opaque) {
    std::string_view built = "!foo!g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv";
    auto elem = NixStringContextElem::parse(built);
    auto * p = std::get_if<NixStringContextElem::Built>(&elem.raw);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->output, "foo");
    ASSERT_EQ(p->drvPath, (SingleDerivedPath::Opaque {
        .path = StorePath { built.substr(5) },
    }));
    ASSERT_EQ(elem.to_string(), built);
}

#ifndef COVERAGE

RC_GTEST_PROP(
    NixStringContextElemTest,
    prop_round_rip,
    (const NixStringContextElem & o))
{
    RC_ASSERT(o == NixStringContextElem::parse(o.to_string()));
}

#endif

}
