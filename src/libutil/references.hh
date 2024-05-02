#pragma once
///@file

#include "hash.hh"

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

struct RewritingSink : Sink
{
    const StringMap rewrites;
    std::string::size_type maxRewriteSize;
    std::string prev;
    Sink & nextSink;

    RewritingSink(const std::string & from, const std::string & to, Sink & nextSink);
    RewritingSink(const StringMap & rewrites, Sink & nextSink);

    void operator () (std::string_view data) override;

    void flush();
};

HashResult computeHashModulo(HashType ht, const std::string & modulus, Source & source);

}
