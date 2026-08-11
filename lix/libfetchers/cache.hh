#pragma once
///@file

#include "lix/libfetchers/fetchers.hh"
#include "lix/libstore/path.hh"
#include <memory>

namespace nix::fetchers {

struct Cache
{
    class Lock
    {
        std::shared_ptr<void> impl;

    public:
        Lock(kj::Badge<Cache>, std::shared_ptr<void> impl) : impl(std::move(impl)) {}
    };

    virtual ~Cache() { }

    virtual kj::Promise<Result<void>>
    add(ref<Store> store,
        const Attrs & inAttrs,
        const Attrs & infoAttrs,
        const StorePath & storePath,
        bool locked) = 0;

    virtual kj::Promise<Result<std::optional<std::pair<Attrs, StorePath>>>> lookup(
        ref<Store> store,
        const Attrs & inAttrs) = 0;

    virtual kj::Promise<Result<std::variant<std::pair<Attrs, StorePath>, Lock>>>
    lookupOrLock(ref<Store> store, const Attrs & inAttrs) = 0;

    struct LookupResult
    {
        bool expired = false;
        Attrs infoAttrs;
        StorePath storePath;
    };

    virtual kj::Promise<Result<std::optional<LookupResult>>> lookupExpired(
        ref<Store> store,
        const Attrs & inAttrs) = 0;

protected:
    static kj::Badge<Cache> badge()
    {
        return {};
    }
};

kj::Promise<Result<ref<Cache>>> getCache();
}
