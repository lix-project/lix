#pragma once
///@file

#include "lix/libutil/hash.hh"

namespace nix {

class RefScanSink : public Sink
{
    StringSet hashes;
    StringSet seen;

    std::string tail;

public:

    RefScanSink(StringSet && hashes) : hashes(hashes)
    { }

    StringSet & getResult()
    { return seen; }

    void operator () (std::string_view data) override;
};

struct RewritingSource : Source
{
    const std::string::size_type maxRewriteSize;
    const std::string initials;
    const StringMap rewrites;
    std::string rewritten, buffered;
    std::string_view unreturned;
    Source * inner;

    static constexpr struct may_change_size_t {
        explicit may_change_size_t() = default;
    } may_change_size{};

    RewritingSource(const std::string & from, const std::string & to, Source & inner);
    RewritingSource(StringMap rewrites, Source & inner);
    RewritingSource(may_change_size_t, StringMap rewrites, Source & inner);

    size_t read(char * data, size_t len) override;
};

HashResult computeHashModulo(HashType ht, const std::string & modulus, Source & source);

}
