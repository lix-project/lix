#pragma once
///@file

#include "lix/libstore/binary-cache-store.hh"

#include <atomic>

namespace nix {

class S3BinaryCacheStore : public virtual BinaryCacheStore
{
protected:

    using BinaryCacheStore::BinaryCacheStore;

public:

    struct Stats
    {
        std::atomic<uint64_t> put{0};
        std::atomic<uint64_t> putBytes{0};
        std::atomic<uint64_t> putTimeMs{0};
        std::atomic<uint64_t> get{0};
        std::atomic<uint64_t> getBytes{0};
        std::atomic<uint64_t> getTimeMs{0};
        std::atomic<uint64_t> head{0};
    };

    virtual const Stats & getS3Stats() = 0;
};

void registerS3BinaryCacheStore();

}
