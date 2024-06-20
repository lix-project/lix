#include "compression.hh"
#include <gtest/gtest.h>

namespace nix {

    /* ----------------------------------------------------------------------------
     * compress / decompress
     * --------------------------------------------------------------------------*/

    TEST(compress, compressWithUnknownMethod) {
        ASSERT_THROW(compress("invalid-method", "something-to-compress"), UnknownCompressionMethod);
    }

    TEST(compress, noneMethodDoesNothingToTheInput) {
        auto o = compress("none", "this-is-a-test");

        ASSERT_EQ(o, "this-is-a-test");
    }

    TEST(decompress, decompressNoneCompressed) {
        auto method = "none";
        auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
        auto o = decompress(method, str);

        ASSERT_EQ(o, str);
    }

    TEST(decompress, decompressEmptyCompressed) {
        // Empty-method decompression used e.g. by S3 store
        // (Content-Encoding == "").
        auto method = "";
        auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
        auto o = decompress(method, str);

        ASSERT_EQ(o, str);
    }

    TEST(decompress, decompressXzCompressed) {
        auto method = "xz";
        auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
        auto o = decompress(method, compress(method, str));

        ASSERT_EQ(o, str);
    }

    TEST(decompress, decompressBzip2Compressed) {
        auto method = "bzip2";
        auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
        auto o = decompress(method, compress(method, str));

        ASSERT_EQ(o, str);
    }

    TEST(decompress, decompressBrCompressed) {
        auto method = "br";
        auto str = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
        auto o = decompress(method, compress(method, str));

        ASSERT_EQ(o, str);
    }

    TEST(decompress, decompressInvalidInputThrowsCompressionError) {
        auto method = "bzip2";
        auto str = "this is a string that does not qualify as valid bzip2 data";

        ASSERT_THROW(decompress(method, str), CompressionError);
    }

    TEST(decompress, veryLongBrotli) {
        auto method = "br";
        auto str = std::string(65536, 'a');
        auto o = decompress(method, compress(method, str));

        // This is just to not print 64k of "a" for most failures
        ASSERT_EQ(o.length(), str.length());
        ASSERT_EQ(o, str);
    }

    /* ----------------------------------------------------------------------------
     * compression sinks
     * --------------------------------------------------------------------------*/

    TEST(makeCompressionSink, noneSinkDoesNothingToInput) {
        StringSink strSink;
        auto inputString = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";
        auto sink = makeCompressionSink("none", strSink);
        (*sink)(inputString);
        sink->finish();

        ASSERT_STREQ(strSink.s.c_str(), inputString);
    }

    TEST(makeCompressionSink, compressAndDecompress) {
        auto inputString = "slfja;sljfklsa;jfklsjfkl;sdjfkl;sadjfkl;sdjf;lsdfjsadlf";

        StringSink strSink;
        auto sink = makeCompressionSink("bzip2", strSink);
        (*sink)(inputString);
        sink->finish();

        StringSource strSource{strSink.s};
        auto decompressionSource = makeDecompressionSource("bzip2", strSource);

        ASSERT_STREQ(decompressionSource->drain().c_str(), inputString);
    }

}
