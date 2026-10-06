#pragma once
//@file

#include "lix/libstore/store-api.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/types.hh"

namespace nix {

struct ClientSettings
{
    Verbosity verbosity = lvlError;
    StringMap overrides;

    void apply(TrustedFlag trusted);
};
}
