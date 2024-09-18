#include "compression.hh"
#include <gtest/gtest.h>

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

    StringSource strSource{strSink.s};
    auto decompressionSource = makeDecompressionSource(method, strSource);

    ASSERT_STREQ(decompressionSource->drain().c_str(), inputString);
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
}
