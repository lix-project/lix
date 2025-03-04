#pragma once
///@file

#include "lix/libstore/fs-accessor.hh"
#include "lix/libutil/ref.hh"
#include "lix/libstore/store-api.hh"

namespace nix {

class RemoteFSAccessor : public FSAccessor
{
    ref<Store> store;

    std::map<std::string, ref<FSAccessor>> nars;

    Path cacheDir;

    kj::Promise<Result<std::pair<ref<FSAccessor>, Path>>>
    fetch(const Path & path_, bool requireValidPath = true);

    friend class BinaryCacheStore;

    Path makeCacheFile(std::string_view hashPart, const std::string & ext);

    kj::Promise<Result<ref<FSAccessor>>> addToCache(std::string_view hashPart, std::string && nar);

public:

    RemoteFSAccessor(ref<Store> store,
        const /* FIXME: use std::optional */ Path & cacheDir = "");

    kj::Promise<Result<Stat>> stat(const Path & path) override;

    kj::Promise<Result<StringSet>> readDirectory(const Path & path) override;

    kj::Promise<Result<std::string>>
    readFile(const Path & path, bool requireValidPath = true) override;

    kj::Promise<Result<std::string>> readLink(const Path & path) override;
};

}
