#pragma once
///@file

#include <vector>

#include "lix/libexpr/pos-idx.hh"
#include "lix/libutil/position.hh"
#include "lix/libutil/sync.hh"

namespace nix {

class PosTable
{
public:
    class Origin
    {
        friend class PosTable;
    private:
        uint32_t base;

        Origin(uint32_t base, uint32_t size) : base(base), size(size) {}

    public:
        const uint32_t size;

        [[gnu::always_inline]]
        PosIdx add(size_t offset)
        {
            if (offset > size) [[unlikely]] {
                return {};
            }
            return PosIdx(1 + base + offset);
        }
    };

private:
    struct Record
    {
        Pos::Origin origin;
        uint32_t base;
        uint32_t size;

        uint32_t offsetOf(PosIdx p) const
        {
            return p.id - 1 - base;
        }
    };

    using Lines = std::vector<uint32_t>;

    std::map<uint32_t, Record> origins;
    mutable Sync<std::map<uint32_t, Lines>> lines;

    const Record * resolve(PosIdx p) const
    {
        if (p.id == 0)
            return nullptr;

        const auto idx = p.id - 1;
        /* we want the last key <= idx, so we'll take prev(first key > idx).
            this is guaranteed to never rewind origin.begin because the first
            key is always 0. */
        const auto pastOrigin = origins.upper_bound(idx);
        return &std::prev(pastOrigin)->second;
    }

public:
    Origin addOrigin(Pos::Origin origin, size_t size)
    {
        uint32_t base = 0;
        if (auto it = origins.rbegin(); it != origins.rend())
            base = it->first + it->second.size;

        // +1 because all PosIdx are offset by 1 to begin with (because noPos == 0), and
        // another +1 to ensure that all origins can point to EOF, eg on (invalid) empty inputs.
        bool saturatedTable = size > UINT32_MAX || 2u + base + size < base;

        if (saturatedTable) {
            return Origin{base, 0};
        } else {
            uint32_t length = static_cast<uint32_t>(size);
            origins.emplace(base, Record{std::move(origin), base, length});
            return Origin{base, length};
        }
    }

    Pos operator[](PosIdx p) const;

    Pos::Origin originOf(PosIdx p) const
    {
        if (auto o = resolve(p))
            return o->origin;
        return std::monostate{};
    }
};

}
