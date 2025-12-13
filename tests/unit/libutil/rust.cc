#include "lix/libutil-rs.gen.hh"
#include <gtest/gtest.h>

namespace nix {
    TEST(rustSupport, testMultiplyArgs) {
        TestMultiplyArgs const args{
            .a = 20,
            .b = 5,
        };

        uint64_t const product = test_multiply(args);

        ASSERT_EQ(product, 20 * 5);
    }
}
