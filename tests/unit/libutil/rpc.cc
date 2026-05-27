#include "lix/libutil/rpc.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/fmt.hh"
#include "lix/libutil/types-rpc.hh"
#include "types.capnp.h"
#include <capnp/blob.h>
#include <capnp/list.h>
#include <concepts>
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>

namespace nix {
TEST(RpcConverters, DISABLED_optionConvertersCompile)
{
    using namespace rpc;

    // primitive inner
    RPC_FILL_STRUCT(Option<OptionInt64>::Builder{nullptr}, initSome, std::optional<int64_t>{});
    static_assert(std::same_as<decltype(from(OptionInt64::Reader{})), std::optional<int64_t>>);

    // string inner, which is special (of course)
    RPC_FILL_STRUCT(Option<Option<capnp::Text>>::Builder{nullptr}, initSome, std::optional<std::string>{});
    static_assert(std::same_as<
                  decltype(to<std::optional<std::string>>(Option<capnp::Text>::Reader{})),
                  std::optional<std::string>>);
    static_assert(std::same_as<
                  decltype(to<std::optional<std::string_view>>(Option<capnp::Text>::Reader{})),
                  std::optional<std::string_view>>);

    // struct inner
    RPC_FILL_STRUCT(Option<Option<rpc::Error>>::Builder{nullptr}, initSome, std::optional<ErrorInfo>{});
    static_assert(std::same_as<decltype(from(Option<rpc::Error>::Reader{})), std::optional<ErrorInfo>>);

    // list inner
    RPC_FILL_STRUCT(
        Option<Option<capnp::List<capnp::Text>>>::Builder{nullptr},
        initSome,
        std::optional<std::list<std::string>>{}
    );
    static_assert(
        std::same_as<
            decltype(to<std::optional<std::list<std::string>>>(Option<capnp::List<capnp::Text>>::Reader{})),
            std::optional<std::list<std::string>>>
    );

    // nested inner
    RPC_FILL_STRUCT(
        Option<Option<OptionInt64>>::Builder{nullptr}, initSome, std::optional<std::optional<int64_t>>{}
    );
    static_assert(
        std::same_as<decltype(from(Option<OptionInt64>::Reader{})), std::optional<std::optional<int64_t>>>
    );
}

TEST(RpcConverters, DISABLED_mapConvertersCompile)
{
    using namespace rpc;

    // primitive args
    RPC_FILL_STRUCT(
        (Option<Map<OptionInt64, OptionInt64>>::Builder{nullptr}),
        initSome,
        (std::map<std::optional<int64_t>, std::optional<int64_t>>{})
    );
    static_assert(std::same_as<
                  decltype(from(Map<OptionInt64, OptionInt64>::Reader{})),
                  std::map<std::optional<int64_t>, std::optional<int64_t>>>);

    // string args
    RPC_FILL_STRUCT(
        (Option<Map<capnp::Data, capnp::Data>>::Builder{nullptr}),
        initSome,
        (std::map<std::string, std::string>{})
    );
    static_assert(std::same_as<
                  decltype(to<std::map<std::string, std::string>>(Map<capnp::Data, capnp::Data>::Reader{})),
                  std::map<std::string, std::string>>);
    static_assert(std::same_as<
                  decltype(to<std::map<std::string_view, std::string_view>>(
                      Map<capnp::Data, capnp::Data>::Reader{}
                  )),
                  std::map<std::string_view, std::string_view>>);

    // struct args
    RPC_FILL_STRUCT(
        (Option<Map<Option<OptionInt64>, Option<OptionInt64>>>::Builder{nullptr}),
        initSome,
        (std::map<std::optional<std::optional<int64_t>>, std::optional<std::optional<int64_t>>>{})
    );
    static_assert(std::same_as<
                  decltype(from(Map<Option<OptionInt64>, Option<rpc::Error>>::Reader{})),
                  std::map<std::optional<std::optional<int64_t>>, std::optional<ErrorInfo>>>);

    // nested args
    RPC_FILL_STRUCT(
        (Option<Map<Map<OptionInt64, OptionInt64>, Map<OptionInt64, OptionInt64>>>::Builder{nullptr}),
        initSome,
        (std::map<
            std::map<std::optional<int64_t>, std::optional<int64_t>>,
            std::map<std::optional<int64_t>, std::optional<int64_t>>>{})
    );
    static_assert(std::same_as<
                  decltype(from(Map<Map<OptionInt64, OptionInt64>, Map<OptionInt64, OptionInt64>>::Reader{})),
                  std::map<
                      std::map<std::optional<int64_t>, std::optional<int64_t>>,
                      std::map<std::optional<int64_t>, std::optional<int64_t>>>>);
}

TEST(RpcErrorV1, shortMessage)
{
    Error e{ErrorInfo{lvlWarn, HintFmt("test message %s", "data")}};
    e.addTrace(nullptr, HintFmt("trace 1 %s", "data"));
    e.addTrace(nullptr, HintFmt("trace 2 %s", "data"));

    const auto encoded = rpc::error::v1::encodeLossy(e.info());
    ASSERT_EQ(
        encoded,
        "test message \x1B[35;1mdata\x1B[0m "
        "{error:ODZmMTlmNjgtMjNiMy00MWE3LTgxYzUtMjY5YWUwN2ZkNDY1Cg:"
        "EBBQAQIBAREF4hERFv90ZXN0IG1lcwJzYWdlIBtbMzU7MW1kYXRhDxtbMG0RBboRDbr/"
        "dHJhY2UgMiAFG1szNTsxbWRhdGEbWzBtAHRyYWNlIDEgG1szNTsxbWRhdGEbWzBtAA==:v1}"
    );

    const auto decoded = rpc::error::v1::tryDecode("remote error: " + encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->level, e.info().level);
    ASSERT_EQ(decoded->msg.str(), e.info().msg.str());
    ASSERT_EQ(decoded->traces.size(), e.info().traces.size());
    for (auto ait = decoded->traces.begin(), bit = e.info().traces.begin(), aend = decoded->traces.end();
         ait != aend;
         ++ait, ++bit)
    {
        ASSERT_EQ(ait->hint.str(), bit->hint.str());
    }
}

TEST(RpcErrorV1, longMessage)
{
    Error e{ErrorInfo{lvlWarn, HintFmt("test message %s", std::string(1024, 'a'))}};
    e.addTrace(nullptr, HintFmt("trace 1 %s", "data"));
    e.addTrace(nullptr, HintFmt("trace 2 %s", "data"));

    const auto encoded = rpc::error::v1::encodeLossy(e.info());
    ASSERT_EQ(
        encoded,
        "(oversize message) "
        "{error:ODZmMTlmNjgtMjNiMy00MWE3LTgxYzUtMjY5YWUwN2ZkNDY1Cg:EI9QAQIBATEFwiATDQIW/"
        "3Rlc3QgbWVzgnNhZ2UgG1szNTsxbWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhY"
        "WFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWFhYWEbWzBtE"
        "QW6EQ26/3RyYWNlIDIgBRtbMzU7MW1kYXRhG1swbQB0cmFjZSAxIBtbMzU7MW1kYXRhG1swbQA=:v1}"
    );

    const auto decoded = rpc::error::v1::tryDecode("remote error: " + encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->level, e.info().level);
    ASSERT_EQ(decoded->msg.str(), e.info().msg.str());
    ASSERT_EQ(decoded->traces.size(), e.info().traces.size());
    for (auto ait = decoded->traces.begin(), bit = e.info().traces.begin(), aend = decoded->traces.end();
         ait != aend;
         ++ait, ++bit)
    {
        ASSERT_EQ(ait->hint.str(), bit->hint.str());
    }
}

TEST(RpcErrorV1, noDecode)
{
    // no marker
    ASSERT_FALSE(
        rpc::error::v1::tryDecode(
            "{rror:ODZmMTlmNjgtMjNiMy00MWE3LTgxYzUtMjY5YWUwN2ZkNDY1Cg:"
            "EBBQAQIBAREF4hERFv90ZXN0IG1lcwJzYWdlIBtbMzU7MW1kYXRhDxtbMG0RBboRDbr/"
            "dHJhY2UgMiAFG1szNTsxbWRhdGEbWzBtAHRyYWNlIDEgG1szNTsxbWRhdGEbWzBtAA==:v1}"
        )
    );
    // no trailer
    ASSERT_FALSE(
        rpc::error::v1::tryDecode(
            "{error:ODZmMTlmNjgtMjNiMy00MWE3LTgxYzUtMjY5YWUwN2ZkNDY1Cg:"
            "EBBQAQIBAREF4hERFv90ZXN0IG1lcwJzYWdlIBtbMzU7MW1kYXRhDxtbMG0RBboRDbr/"
            "dHJhY2UgMiAFG1szNTsxbWRhdGEbWzBtAHRyYWNlIDEgG1szNTsxbWRhdGEbWzBtAA==}"
        )
    );
    // bad base64
    ASSERT_FALSE(
        rpc::error::v1::tryDecode(
            "{error:ODZmMTlmNjgtMjNiMy00MWE3LTgxYzUtMjY5YWUwN2ZkNDY1Cg:"
            "EBBQAQIBAREF4hERFv90ZXN0IG1lcwJzYWdlIBtbMzU7MW1kYXRhDxtbMG0RBboRDbr/"
            "dHJhY2UgMiAFG1szNTsxbWRhdGEbWzBtAHRyYWNlIDEgG1szNTsxbWRhdGEbWzBtAA=:v1}"
        )
    );
    // bad capnp data
    ASSERT_FALSE(
        rpc::error::v1::tryDecode(
            "{error:ODZmMTlmNjgtMjNiMy00MWE3LTgxYzUtMjY5YWUwN2ZkNDY1Cg:"
            "eBBQAQIBAREF4hERFv90ZXN0IG1lcwJzYWdlIBtbMzU7MW1kYXRhDxtbMG0RBboRDbr/"
            "dHJhY2UgMiAFG1szNTsxbWRhdGEbWzBtAHRyYWNlIDEgG1szNTsxbWRhdGEbWzBtAA==:v1}"
        )
    );
}
}
