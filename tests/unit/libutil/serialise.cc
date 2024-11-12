#include "lix/libutil/serialise.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/fmt.hh"
#include "lix/libexpr/pos-table.hh"
#include "lix/libutil/generator.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/types.hh"

#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <limits.h>
#include <gtest/gtest.h>

#include <numeric>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace nix {

// don't deduce the type of `val` for added insurance.
template<typename T>
static std::string toWire(const std::type_identity_t<T> & val)
{
    std::string result;
    auto g = [] (const auto & val) -> WireFormatGenerator { co_yield val; }(val);
    while (auto buffer = g.next()) {
        result.append(buffer->data(), buffer->size());
    }
    return result;
}

TEST(WireFormatGenerator, uint64_t)
{
    auto s = toWire<uint64_t>(42);
    ASSERT_EQ(s, std::string({42, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(WireFormatGenerator, string_view)
{
    auto s = toWire<std::string_view>("");
    // clang-format off
    ASSERT_EQ(
        s,
        std::string({
            // length
            0, 0, 0, 0, 0, 0, 0, 0,
            // data (omitted)
        })
    );
    // clang-format on

    s = toWire<std::string_view>("test");
    // clang-format off
    ASSERT_EQ(
        s,
        std::string({
            // length
            4, 0, 0, 0, 0, 0, 0, 0,
            // data
            't', 'e', 's', 't',
            // padding
            0, 0, 0, 0,
        })
    );
    // clang-format on

    s = toWire<std::string_view>("longer string");
    // clang-format off
    ASSERT_EQ(
        s,
        std::string({
            // length
            13, 0, 0, 0, 0, 0, 0, 0,
            // data
            'l', 'o', 'n', 'g', 'e', 'r', ' ', 's', 't', 'r', 'i', 'n', 'g',
            // padding
            0, 0, 0,
        })
    );
    // clang-format on
}

TEST(WireFormatGenerator, StringSet)
{
    auto s = toWire<StringSet>({});
    // clang-format off
    ASSERT_EQ(
        s,
        std::string({
            // length
            0, 0, 0, 0, 0, 0, 0, 0,
            // data (omitted)
        })
    );
    // clang-format on

    s = toWire<StringSet>({"a", ""});
    // clang-format off
    ASSERT_EQ(
        s,
        std::string({
            // length
            2, 0, 0, 0, 0, 0, 0, 0,
            // data ""
            0, 0, 0, 0, 0, 0, 0, 0,
            // data "a"
            1, 0, 0, 0, 0, 0, 0, 0, 'a', 0, 0, 0, 0, 0, 0, 0,
        })
    );
    // clang-format on
}

TEST(WireFormatGenerator, Strings)
{
    auto s = toWire<Strings>({});
    // clang-format off
    ASSERT_EQ(
        s,
        std::string({
            // length
            0, 0, 0, 0, 0, 0, 0, 0,
            // data (omitted)
        })
    );
    // clang-format on

    s = toWire<Strings>({"a", ""});
    // clang-format off
    ASSERT_EQ(
        s,
        std::string({
            // length
            2, 0, 0, 0, 0, 0, 0, 0,
            // data "a"
            1, 0, 0, 0, 0, 0, 0, 0, 'a', 0, 0, 0, 0, 0, 0, 0,
            // data ""
            0, 0, 0, 0, 0, 0, 0, 0,
        })
    );
    // clang-format on
}

TEST(WireFormatGenerator, Error)
{
    PosTable pt;
    auto o = pt.addOrigin(Pos::String{make_ref<std::string>("test")}, 4);

    auto s = toWire<Error>(Error{ErrorInfo{
        .level = lvlInfo,
        .msg = HintFmt("foo"),
        .pos = pt[pt.add(o, 1)],
        .traces = {{.pos = pt[pt.add(o, 2)], .hint = HintFmt("b %1%", "foo")}},
    }});
    // NOTE position of the error and all traces are ignored
    // by the wire format
    // clang-format off
    ASSERT_EQ(
        s,
        std::string({
            5, 0, 0, 0, 0, 0, 0, 0, 'E', 'r', 'r', 'o', 'r', 0, 0, 0,
            3, 0, 0, 0, 0, 0, 0, 0,
            5, 0, 0, 0, 0, 0, 0, 0, 'E', 'r', 'r', 'o', 'r', 0, 0, 0,
            3, 0, 0, 0, 0, 0, 0, 0, 'f', 'o', 'o', 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            1, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,
            16, 0, 0, 0, 0, 0, 0, 0,
            'b', ' ', '\x1b', '[', '3', '5', ';', '1', 'm', 'f', 'o', 'o', '\x1b', '[', '0', 'm',
        })
    );
    // clang-format on
}

TEST(WireFormatGenerator, exampleMessage)
{
    auto gen = []() -> WireFormatGenerator {
        std::set<std::string> foo{"a", "longer string", ""};
        co_yield 42;
        co_yield foo;
        co_yield std::string_view("test");
        co_yield true;
    }();

    std::vector<char> full;
    while (auto s = gen.next()) {
        full.insert(full.end(), s->begin(), s->end());
    }

    ASSERT_EQ(
        full,
        (std::vector<char>{
            // clang-format off
            // 42
            42, 0, 0, 0, 0, 0, 0, 0,
            // foo
            3, 0, 0, 0, 0, 0, 0, 0,
            /// ""
            0, 0, 0, 0, 0, 0, 0, 0,
            /// a
            1, 0, 0, 0, 0, 0, 0, 0,
            'a', 0, 0, 0, 0, 0, 0, 0,
            /// longer string
            13, 0, 0, 0, 0, 0, 0, 0,
            'l', 'o', 'n', 'g', 'e', 'r', ' ', 's', 't', 'r', 'i', 'n', 'g', 0, 0, 0,
            // foo done
            // test
            4, 0, 0, 0, 0, 0, 0, 0,
            't', 'e', 's', 't', 0, 0, 0, 0,
            // true
            1, 0, 0, 0, 0, 0, 0, 0,
            //clang-format on
            }));
}

TEST(GeneratorSource, works)
{
    GeneratorSource src = []() -> Generator<Bytes> {
        co_yield std::span{"", 0};
        co_yield std::span{"a", 1};
        co_yield std::span{"", 0};
        co_yield std::span{"bcd", 3};
        co_yield std::span{"", 0};
    }();

    char buf[2];
    ASSERT_EQ(src.read(buf, sizeof(buf)), 1);
    ASSERT_EQ(buf[0], 'a');
    ASSERT_EQ(src.read(buf, sizeof(buf)), 2);
    ASSERT_EQ(buf[0], 'b');
    ASSERT_EQ(buf[1], 'c');
    ASSERT_EQ(src.read(buf, sizeof(buf)), 1);
    ASSERT_EQ(buf[0], 'd');
    ASSERT_THROW(src.read(buf, sizeof(buf)), EndOfFile);
}

}
