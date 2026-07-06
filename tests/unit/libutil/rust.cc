#include "lix/libutil/error.hh"
#include "lix/libutil/types.hh"
#include "lix/lix-rs/main.gen.hh"
#include "lix/lix-rs/utils.hh"
#include "zngur.gen.hh"
#include "gtest/gtest.h"
#include <cstdint>
#include <gtest/gtest.h>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

namespace nix {

TEST(rustSupport, testMultiplyAddLen)
{
    auto vec = rust::Vec<rust::String>::new_();
    vec.push(rust::String::from("1"_rs));
    vec.push(rust::String::from("2"_rs));
    vec.push(rust::String::from("3"_rs));
    vec.push(rust::String::from("4"_rs));
    vec.push(rust::String::from("5"_rs));

    auto args = ffi_test::TestMultiplyAddLenArgs::new_(20, 1);
    args.set_b(5);
    auto product = ffi_test::test_multiply_add_len(args, std::move(vec));

    ASSERT_EQ(to_std_string(product.f0.as_str()), R"-((20 * 5 + ["1", "2", "3", "4", "5"].len()) = 105)-");
    ASSERT_EQ(product.f1, 20 * 5 + 5);
}

TEST(rustSupport, testResult)
{
    auto result = ffi_test::test_result();
    ASSERT_TRUE(result.matches_Err());
    EXPECT_DEATH(result.unwrap(), "called `Result::unwrap\\(\\)` on an `Err`");
    auto msg = to_std_string(result.unwrap_err().to_string());
    ASSERT_EQ(msg, "errors travel freely");

    std::visit(
        overloaded{
            [](rust::match<rust::matches::Ok, rust::Unit>) { FAIL(); },
            [](rust::match<rust::matches::Err, rust::Box<rust::Dyn<rust::std::error::Error>>> e) {
                auto msg = to_std_string(e.value.to_string());
                ASSERT_EQ(msg, "errors travel freely");
            }
        },
        to_std(ffi_test::test_result())
    );

    std::visit(
        overloaded{
            [](rust::Unit) { FAIL(); },
            [](rust::Box<rust::Dyn<rust::std::error::Error>> e) {
                auto msg = to_std_string(e.to_string());
                ASSERT_EQ(msg, "errors travel freely");
            }
        },
        to_std(ffi_test::test_result())
    );

    match_result(
        ffi_test::test_result(),
        [](rust::Unit) { FAIL(); },
        [](rust::Box<rust::Dyn<rust::std::error::Error>> e) {
            auto msg = to_std_string(e.to_string());
            ASSERT_EQ(msg, "errors travel freely");
        }
    );
}

TEST(rustSupport, testOption)
{
    ASSERT_EQ(to_std(ffi_test::test_option_some()), 1);
    ASSERT_FALSE(to_std(ffi_test::test_option_none()).has_value());
}

TEST(rustSupport, testResultFromCxx)
{
    using rust::make_box_fn, ffi_test::test_exceptions;

    auto result = test_exceptions(make_box_fn([]() -> ::rust::Unit { throw Error("test"); }));
    ASSERT_EQ(to_std_string(result), "\x1B[31;1merror:\x1B[0m test");

    result = test_exceptions(make_box_fn([] { throw Error("test"); }));
    ASSERT_EQ(to_std_string(result), "\x1B[31;1merror:\x1B[0m test");

    result = test_exceptions(make_box_fn([] {}));
    ASSERT_EQ(to_std_string(result), "");
}

TEST(rustSupport, testOperators)
{
    using rust::lix::ffi_test::TestMultiplyAddLenArgs;

    auto args1 = TestMultiplyAddLenArgs::new_(1, 2);
    auto args2 = TestMultiplyAddLenArgs::new_(1, 3);

    ASSERT_LT(args1, args2);
    ASSERT_LE(args1, args1);
    ASSERT_LE(args1, args2);

    ASSERT_GT(args2, args1);
    ASSERT_GE(args1, args1);
    ASSERT_GE(args2, args1);

    ASSERT_EQ(args1, args1);
    ASSERT_NE(args1, args2);

    std::map<TestMultiplyAddLenArgs, int> map{{args2, 2}, {args1, 1}};

    ASSERT_EQ(map.begin()->first, args1);
    ASSERT_EQ(map.begin()->second, 1);
    ASSERT_EQ(map.rbegin()->first, args2);
    ASSERT_EQ(map.rbegin()->second, 2);

    std::set<TestMultiplyAddLenArgs> set;

    set.emplace(args1);
    set.emplace(args1);
    set.emplace(args1);
    set.emplace(args1);

    ASSERT_EQ(set.size(), 1);
}

TEST(rustSupport, iterators)
{
    auto vec = rust::Vec<uint8_t>::new_();
    vec.push(1);
    vec.push(2);
    vec.push(3);
    vec.push(4);
    vec.push(5);

    int i = 0;
    for (auto u : vec.as_ref().iter()) {
        EXPECT_EQ(*u, ++i);
    }
    for (auto u : vec.as_mut().iter_mut()) {
        (*u)++;
    }
    i = 1;
    for (auto u : vec.as_ref().iter()) {
        EXPECT_EQ(*u, ++i);
    }
    i = 1;
    for (auto u : vec.into_iter()) {
        EXPECT_EQ(u, ++i);
    }
}
}
