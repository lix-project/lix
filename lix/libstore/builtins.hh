#pragma once
///@file

#include "lix/libutil/async.hh"
#include "lix/libutil/hash.hh"
#include "lix/libutil/types.hh"

namespace nix {

// TODO: make pluggable.
struct BuiltinFetchurl
{
    Path storePath;
    std::string mainUrl;
    bool unpack;
    bool executable;
    std::optional<Hash> hash;

    void run(AsyncIoRoot & aio);
};

void builtinUnpackChannel(const Path & out, const std::string & channelName, const std::string & src);
}
