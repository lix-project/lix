#pragma once
///@file

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
    std::string netrcData;
    std::string caFileData;

    void run();
};

void builtinUnpackChannel(const Path & out, const std::string & channelName, const std::string & src);
}
