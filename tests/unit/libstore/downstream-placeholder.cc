#include <gtest/gtest.h>

#include "lix/libstore/downstream-placeholder.hh"

namespace nix {

TEST(DownstreamPlaceholder, unknownCaOutput) {
    /**
     * We set these in tests rather than the regular globals so we don't have
     * to worry about race conditions if the tests run concurrently.
     */
    ExperimentalFeatureSettings mockXpSettings;
    mockXpSettings.set("experimental-features", "ca-derivations");

    ASSERT_EQ(
        DownstreamPlaceholder::unknownCaOutput(
            StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv" },
            "out",
            mockXpSettings).render(),
        "/0c6rn30q4frawknapgwq386zq358m8r6msvywcvc89n6m5p2dgbz");
}

}
