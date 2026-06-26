#include "lix/libutil/error.hh"
#include "lix/libutil/types.hh"
#include "lix/lix-rs/main.gen.hh"
#include "lix/lix-rs/utils.hh"
#include "zngur.gen.hh"
#include "gtest/gtest.h"
#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <variant>

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
}
