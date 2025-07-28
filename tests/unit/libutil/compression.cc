#include "lix/libutil/compression.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/serialise.hh"
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>

namespace nix {

/* ----------------------------------------------------------------------------
 * compress / decompress
 * --------------------------------------------------------------------------*/

TEST(compress, compressWithUnknownMethod)
{
    ASSERT_THROW(compress("invalid-method", "something-to-compress"), UnknownCompressionMethod);
}

TEST(compress, noneMethodDoesNothingToTheInput)
{
    auto o = compress("none", "this-is-a-test");

    ASSERT_EQ(o, "this-is-a-test");
}

TEST(decompress, decompressEmptyString)
{
    // Empty-method decompression used e.g. by S3 store
    // (Content-Encoding == "").
    auto o = decompress("", "this-is-a-test");

    ASSERT_EQ(o, "this-is-a-test");
}

/* ----------------------------------------------------------------------------
 * compression sinks
 * --------------------------------------------------------------------------*/

TEST(makeCompressionSink, noneSinkDoesNothingToInput)
{
    auto method = "none";
    StringSink strSink;
    auto inputString = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto sink = makeCompressionSink(method, strSink);
    (*sink)(inputString);
    sink->finish();

    ASSERT_STREQ(strSink.s.c_str(), inputString);
}

/** Tests applied to all compression types */
class PerTypeCompressionTest : public testing::TestWithParam<const char *>
{};

/** Tests applied to non-passthrough compression types */
class PerTypeNonNullCompressionTest : public testing::TestWithParam<const char *>
{};

constexpr const char * COMPRESSION_TYPES_NONNULL[] = {
    // libarchive
    "bzip2",
    "compress",
    "gzip",
    "lzip",
    "lzma",
    "xz",
    "zstd",
    // Uses external program via libarchive so cannot be used :(
    /*
    "grzip",
    "lrzip",
    "lzop",
    "lz4",
    */
    // custom
    "br",
};

INSTANTIATE_TEST_SUITE_P(
    compressionNonNull, PerTypeNonNullCompressionTest, testing::ValuesIn(COMPRESSION_TYPES_NONNULL)
);
INSTANTIATE_TEST_SUITE_P(
    compressionNonNull, PerTypeCompressionTest, testing::ValuesIn(COMPRESSION_TYPES_NONNULL)
);

INSTANTIATE_TEST_SUITE_P(
    compressionNull, PerTypeCompressionTest, testing::Values("none")
);

/* ---------------------------------------
 * All compression types
 * --------------------------------------- */

TEST_P(PerTypeCompressionTest, roundTrips)
{
    auto method = GetParam();
    auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
    auto o = decompress(method, compress(method, str));

    ASSERT_EQ(o, str);
}

TEST_P(PerTypeCompressionTest, longerThanBuffer)
{
    // This is targeted originally at regression testing a brotli bug, but we might as well do it to
    // everything
    auto method = GetParam();
    auto str = std::string(65536, 'a');
    auto o = decompress(method, compress(method, str));

    // This is just to not print 64k of "a" for most failures
    ASSERT_EQ(o.length(), str.length());
    ASSERT_EQ(o, str);
}

TEST_P(PerTypeCompressionTest, sinkAndSource)
{
    auto method = GetParam();
    auto inputString = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";

    StringSink strSink;
    auto sink = makeCompressionSink(method, strSink);
    (*sink)(inputString);
    sink->finish();

    auto decompressionSource =
        makeDecompressionSource(method, std::make_unique<StringSource>(strSink.s));

    ASSERT_STREQ(decompressionSource->drain().c_str(), inputString);
}

TEST_P(PerTypeCompressionTest, sinkAndAsyncStream)
{
    AsyncIoRoot aio;

    auto method = GetParam();
    auto inputString = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";

    StringSink strSink;
    auto sink = makeCompressionSink(method, strSink);
    (*sink)(inputString);
    sink->finish();

    auto decompressionStream =
        makeDecompressionStream(method, make_box_ptr<AsyncStringInputStream>(strSink.s));

    ASSERT_STREQ(aio.blockOn(decompressionStream->drain()).c_str(), inputString);
}

/* ---------------------------------------
 * Non null compression types
 * --------------------------------------- */

TEST_P(PerTypeNonNullCompressionTest, bogusInputDecompression)
{
    auto param = GetParam();

    auto bogus = "this data is bogus and should throw when decompressing";
    ASSERT_THROW(decompress(param, bogus), CompressionError);
}

TEST_P(PerTypeNonNullCompressionTest, truncatedValidInput)
{
    auto method = GetParam();

    auto inputString = "the quick brown fox jumps over the lazy doggos";
    auto compressed = compress(method, inputString);

    /* n.b. This also tests zero-length input, which is also invalid.
     * As of the writing of this comment, it returns empty output, but is
     * allowed to throw a compression error instead. */
    for (size_t i = 0u; i < compressed.length(); ++i) {
        auto newCompressed = compressed.substr(compressed.length() - i);
        try {
            decompress(method, newCompressed);
            // Success is acceptable as well, even though it is corrupt data.
            // The compression method is not expected to provide integrity,
            // just, not break explosively on bad input.
        } catch (CompressionError &) {
            // Acceptable
        }
    }
}

}
