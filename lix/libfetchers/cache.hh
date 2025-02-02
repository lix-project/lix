#pragma once
///@file

#include "lix/libfetchers/fetchers.hh"
#include "lix/libstore/path.hh"

namespace nix::fetchers {

struct Cache
{
    virtual ~Cache() { }

    virtual void add(
        ref<Store> store,
        const Attrs & inAttrs,
        const Attrs & infoAttrs,
        const StorePath & storePath,
        bool locked) = 0;

    virtual kj::Promise<Result<std::optional<std::pair<Attrs, StorePath>>>> lookup(
        ref<Store> store,
        const Attrs & inAttrs) = 0;

    struct LookupResult
    {
        bool expired = false;
        Attrs infoAttrs;
        StorePath storePath;
    };

    virtual kj::Promise<Result<std::optional<LookupResult>>> lookupExpired(
        ref<Store> store,
        const Attrs & inAttrs) = 0;
};

ref<Cache> getCache();

}
