#pragma once
///@file

#include "lix/libutil/types.hh"
#include "lix/libutil/hash.hh"
#include "lix/libstore/path-info.hh"

namespace nix {

class Store;

struct NarInfo : ValidPathInfo
{
    std::string url;
    std::string compression;
    std::optional<Hash> fileHash;
    uint64_t fileSize = 0;

    NarInfo() = delete;
    NarInfo(const Store & store, std::string && name, ContentAddressWithReferences && ca, Hash narHash)
        : ValidPathInfo(store, std::move(name), std::move(ca), narHash)
    { }
    NarInfo(StorePath && path, Hash narHash) : ValidPathInfo(std::move(path), narHash) { }
    NarInfo(const ValidPathInfo & info) : ValidPathInfo(info) { }
    NarInfo(const Store & store, const std::string_view & s, const std::string_view & whence);

    std::string to_string(const Store & store) const;
};

}
