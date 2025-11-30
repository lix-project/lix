#pragma once
///@file

#include "lix/libutil/types.hh"

namespace nix {
/**
 * This returns a list of compile-time features.
 *
 * Historically, `signed-caches` seemed to have been one.
 * So we include it always.
 *
 * NOTE: There's no guarantee that the output will be stable
 * across Lix versions.
 */
constexpr Strings getNixFeatures()
{
    Strings features = {"signed-caches"};

#if HAVE_BOEHMGC
    features.push_back("gc");
#endif

    return features;
}
}
