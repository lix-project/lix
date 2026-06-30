#include "lix/libutil/error.hh"
#include "lix/libutil/fmt.hh"
#include "lix/libutil/types-rpc.hh"
#include <gtest/gtest.h>

namespace nix {
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
