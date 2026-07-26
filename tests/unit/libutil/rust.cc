#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/types.hh"
#include "lix/lix-rs/main.gen.hh"
#include "lix/lix-rs/utils.hh"
#include "zngur.gen.hh"
#include "gtest/gtest.h"
#include <cstdint>
#include <exception>
#include <gtest/gtest.h>
#include <kj/async.h>
#include <kj/exception.h>
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
    ASSERT_TRUE(decltype(result)::Err::check(result));
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

TEST(rustAsync, asyncWakeup)
{
    AsyncIoRoot aio;
    auto future = ffi_test::wakes_self();

    {
        auto paf = kj::newPromiseAndFulfiller<void>();
        auto waker = futures::Waker::build(std::move(paf.fulfiller));
        ASSERT_FALSE(to_std(future.poll(*waker)).has_value());
        ASSERT_TRUE(paf.promise.poll(aio.kj.waitScope));
    }

    {
        auto paf = kj::newPromiseAndFulfiller<void>();
        auto waker = futures::Waker::build(std::move(paf.fulfiller));
        ASSERT_EQ(to_std(future.poll(*waker)), 1);
    }
}

TEST(rustAsync, asyncWakeupFromThread)
{
    // rust requires wakers to be Send + Sync. this is a sanity check for that
    AsyncIoRoot aio;
    ASSERT_EQ(to_kj(ffi_test::wakes_from_thread()).wait(aio.kj.waitScope), 9001);
}

TEST(rustAsync, rustToCpp)
{
    AsyncIoRoot aio;

    // if this passes we'll just believe that scheduling works
    ASSERT_EQ(to_kj(ffi_test::wakes_self()).wait(aio.kj.waitScope), 1);
}

TEST(rustAsync, cppToRustPlainMustWait)
{
    AsyncIoRoot aio;
    auto paf = kj::newPromiseAndFulfiller<int>();
    auto p = futures::to_rust(std::move(paf.promise));
    auto f = to_kj(ffi_test::await_add_one(std::move(p)));
    // shared state should be Initial
    ASSERT_FALSE(f.poll(aio.kj.waitScope));
    // now it should be Waiting
    paf.fulfiller->fulfill(2);
    // and now Ready
    ASSERT_EQ(f.wait(aio.kj.waitScope).unwrap(), 3);
}

TEST(rustAsync, cppToRustPlainAlreadyDone)
{
    AsyncIoRoot aio;
    auto paf = kj::newPromiseAndFulfiller<int>();
    auto p = futures::to_rust(std::move(paf.promise));
    auto f = to_kj(ffi_test::await_add_one(std::move(p)));
    paf.fulfiller->fulfill(2);
    // make sure the promise actually gets the memo
    aio.kj.waitScope.poll();
    // shared state should be Ready now
    ASSERT_EQ(f.wait(aio.kj.waitScope).unwrap(), 3);
}

TEST(rustAsync, cppToRustKjException)
{
    AsyncIoRoot aio;
    auto paf = kj::newPromiseAndFulfiller<int>();
    auto p = futures::to_rust(std::move(paf.promise));
    auto f = to_kj(ffi_test::await_add_one(std::move(p)));
    paf.fulfiller->reject(kj::Exception(kj::Exception::Type::FAILED, kj::str("file"), 0, kj::str("snafu")));
    ASSERT_EQ(to_std_string(f.wait(aio.kj.waitScope).unwrap_err().to_string()), "\n \xE2\x97\x8F snafu\n");
}

TEST(rustAsync, cppToRustError)
{
    AsyncIoRoot aio;
    auto paf = kj::newPromiseAndFulfiller<Result<int>>();
    auto p = futures::to_rust(std::move(paf.promise));
    auto f = to_kj(ffi_test::await_add_one(std::move(p)));
    paf.fulfiller->fulfill(std::make_exception_ptr(Error("snafu")));
    ASSERT_EQ(
        to_std_string(f.wait(aio.kj.waitScope).unwrap_err().to_string()),
        "\n \xE2\x97\x8F \x1B[31;1merror:\x1B[0m snafu\n"
    );
}

TEST(rustAsync, cppToRustCancel)
{
    AsyncIoRoot aio;
    auto paf = kj::newPromiseAndFulfiller<int>();
    // dropping a wrapping future must also cancel the wrapped promise
    (void) futures::to_rust(std::move(paf.promise));
    ASSERT_FALSE(paf.fulfiller->isWaiting());
}
}
