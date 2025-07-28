#include "lix/libutil/references.hh"
#include "lix/libutil/hash.hh"
#include "lix/libutil/logging.hh"

#include <cstdlib>
#include <mutex>
#include <algorithm>


namespace nix {


static size_t refLength = 32; /* characters */


static void search(
    std::string_view s,
    StringSet & hashes,
    StringSet & seen)
{
    static std::once_flag initialised;
    static bool isBase32[256];
    std::call_once(initialised, [](){
        for (unsigned int i = 0; i < 256; ++i) isBase32[i] = false;
        for (unsigned int i = 0; i < base32Chars.size(); ++i)
            isBase32[(unsigned char) base32Chars[i]] = true;
    });

    for (size_t i = 0; i + refLength <= s.size(); ) {
        int j;
        bool match = true;
        for (j = refLength - 1; j >= 0; --j)
            if (!isBase32[(unsigned char) s[i + j]]) {
                i += j + 1;
                match = false;
                break;
            }
        if (!match) continue;
        std::string ref(s.substr(i, refLength));
        if (hashes.erase(ref)) {
            debug("found reference to '%1%' at offset '%2%'", ref, i);
            seen.insert(ref);
        }
        ++i;
    }
}


void RefScanSink::operator () (std::string_view data)
{
    /* It's possible that a reference spans the previous and current
       fragment, so search in the concatenation of the tail of the
       previous fragment and the start of the current fragment. */
    auto s = tail;
    auto tailLen = std::min(data.size(), refLength);
    s.append(data.data(), tailLen); // NOLINT(bugprone-suspicious-stringview-data-usage)
    search(s, hashes, seen);

    search(data, hashes, seen);

    auto rest = refLength - tailLen;
    if (rest < tail.size())
        tail = tail.substr(tail.size() - rest);
    tail.append(data.data() + data.size() - tailLen, tailLen);
}


RewritingSource::RewritingSource(const std::string & from, const std::string & to, Source & inner)
    : RewritingSource({{from, to}}, inner)
{
}

RewritingSource::RewritingSource(StringMap rewrites, Source & inner)
    : RewritingSource(may_change_size, std::move(rewrites), inner)
{
    for (auto & [from, to] : this->rewrites) {
        assert(from.size() == to.size());
    }
}

RewritingSource::RewritingSource(may_change_size_t, StringMap rewrites, Source & inner)
    : maxRewriteSize([&, result = size_t(0)]() mutable {
        for (auto & [k, v] : rewrites) {
            result = std::max(result, k.size());
        }
        return result;
    }())
    , initials([&]() -> std::string {
        std::string initials;
        for (const auto & [k, v] : rewrites) {
            assert(!k.empty());
            initials.push_back(k[0]);
        }
        std::ranges::sort(initials);
        auto [firstDupe, _end] = std::ranges::unique(initials);
        return {initials.begin(), firstDupe};
    }())
    , rewrites(std::move(rewrites))
    , inner(&inner)
{
}

size_t RewritingSource::read(char * data, size_t len)
{
    if (rewrites.empty()) {
        return inner->read(data, len);
    }

    if (unreturned.empty()) {
        // always make sure to have at least *two* full rewrites in the buffer,
        // otherwise we may end up incorrectly rewriting if the replacement map
        // contains keys that are proper infixes of other keys in the map. take
        // for example the set { ab -> cc, babb -> bbbb } on the input babb. if
        // we feed the input bytewise without additional windowing we will miss
        // the full babb match once the second b has been seen and bab has been
        // rewritten to ccb, even though babb occurs first in the input string.
        while (inner && buffered.size() < std::max(2 * maxRewriteSize, len)) {
            try {
                auto read = inner->read(data, std::min(2 * maxRewriteSize, len));
                buffered.append(data, read);
            } catch (EndOfFile &) {
                inner = nullptr;
            }
        }

        if (buffered.empty() && !inner) {
            throw EndOfFile("rewritten source exhausted");
        }

        const size_t reserved = inner ? maxRewriteSize : 0;
        size_t j = 0;
        while ((j = buffered.find_first_of(initials, j)) < buffered.size() - reserved) {
            size_t skip = 1;
            for (const auto & [from, to] : rewrites) {
                if (buffered.compare(j, from.size(), from) == 0) {
                    buffered.replace(j, from.size(), to);
                    skip = to.size();
                    break;
                }
            }
            j += skip;
        }

        rewritten = std::move(buffered);
        buffered = rewritten.substr(rewritten.size() - reserved);
        unreturned = rewritten;
        unreturned.remove_suffix(reserved);
    }

    len = std::min(len, unreturned.size());
    memcpy(data, unreturned.data(), len);
    unreturned.remove_prefix(len);
    return len;
}

HashResult computeHashModulo(HashType ht, const std::string & modulus, Source & source)
{
    struct LengthSink : Sink
    {
        Sink & inner;
        uint64_t length = 0;

        LengthSink(Sink & inner) : inner(inner) {}

        void operator()(std::string_view data) override
        {
            length += data.size();
            inner(data);
        }
    };

    HashSink hashSink(ht);
    RewritingSource rewritingSource(modulus, std::string(modulus.size(), 0), source);
    LengthSink lengthSink{hashSink};

    rewritingSource.drainInto(lengthSink);

    /* Hash the positions of the self-references. This ensures that a
       NAR with self-references and a NAR with some of the
       self-references already zeroed out do not produce a hash
       collision. FIXME: proof. */
    // NOTE(horrors) RewritingSink didn't track any matches!
    //for (auto & pos : rewritingSource.matches)
    //    hashSink(fmt("|%d", pos));

    auto h = hashSink.finish();
    return {h.first, lengthSink.length};
}

}
