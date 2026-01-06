#pragma once
///@file

#include <openssl/evp.h>

#include "lix/libutil/archive.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/file-system.hh"

namespace nix {

namespace detail {

using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

}

MakeError(BadHash, Error);

enum class HashType : char { MD5 = 42, SHA1, SHA256, SHA512 };

const size_t md5HashSize = 16;
const size_t sha1HashSize = 20;
const size_t sha256HashSize = 32;
const size_t sha512HashSize = 64;

static constexpr size_t regularHashSize(HashType type)
{
    switch (type) {
    case HashType::MD5:
        return md5HashSize;
    case HashType::SHA1:
        return sha1HashSize;
    case HashType::SHA256:
        return sha256HashSize;
    case HashType::SHA512:
        return sha512HashSize;
    }
    abort();
}

extern const std::set<std::string> hashTypes;

/**
 * @brief Enumeration representing the hash formats.
 */
enum class HashFormat : int {
    /// @brief Base 64 encoding.
    /// @see [IETF RFC 4648, section 4](https://datatracker.ietf.org/doc/html/rfc4648#section-4).
    Base64,
    /// @brief Nix-specific base-32 encoding. @see base32Chars
    Base32,
    /// @brief Lowercase hexadecimal encoding. @see base16Chars
    Base16,
    /// @brief "<hash algo>:<Base 64 hash>", format of the SRI integrity attribute.
    /// @see W3C recommendation [Subresource Intergrity](https://www.w3.org/TR/SRI/).
    SRI
};

struct Hash
{
    constexpr static size_t maxHashSize = 64;
    size_t hashSize = 0;
    uint8_t hash[maxHashSize] = {};

    HashType type;

    /**
     * Create a zero-filled hash object.
     */
    Hash(size_t hashSize, HashType type) : hashSize(hashSize), type(type)
    {
        assert(hashSize <= maxHashSize);
        memset(hash, 0, maxHashSize);
    }

    Hash(HashType type) : Hash(regularHashSize(type), type) {}

    /**
     * Parse the hash from a string representation in the format
     * "[<type>:]<base16|base32|base64>" or "<type>-<base64>" (a
     * Subresource Integrity hash expression). If the 'type' argument
     * is not present, then the hash type must be specified in the
     * string.
     */
    static Hash parseAny(std::string_view s, std::optional<HashType> type);

    /**
     * Parse a hash from a string representation like the above, except the
     * type prefix is mandatory is there is no separate arguement.
     */
    static Hash parseAnyPrefixed(std::string_view s);

    /**
     * Parse a plain hash that musst not have any prefix indicating the type.
     * The type is passed in to disambiguate.
     */
    static Hash parseNonSRIUnprefixed(std::string_view s, HashType type);

    static Hash parseSRI(std::string_view original);

private:
    /**
     * The type must be provided, the string view must not include <type>
     * prefix. `isSRI` helps disambigate the various base-* encodings.
     */
    Hash(std::string_view s, HashType type, bool isSRI);

public:
    /**
     * Check whether two hash are equal.
     */
    bool operator==(const Hash & other) const
    {
        return std::ranges::equal(as_span(), other.as_span());
    }

    /**
     * For sorting.
     */
    std::strong_ordering operator<=>(const Hash & other) const
    {
        std::span<const uint8_t> lhs = as_span(), rhs = other.as_span();

        return (lhs.size() == rhs.size())
            ? std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end())
            : lhs.size() <=> rhs.size();
    }

    /**
     * Returns the length of a base-16 representation of this hash.
     */
    size_t base16Len() const
    {
        return hashSize * 2;
    }

    /**
     * Returns the length of a base-32 representation of this hash.
     */
    size_t base32Len() const
    {
        return (hashSize * 8 - 1) / 5 + 1;
    }

    /**
     * Returns the length of a base-64 representation of this hash.
     */
    size_t base64Len() const
    {
        return ((4 * hashSize / 3) + 3) & ~3;
    }

    /**
     * Returns a span of the intrinsic hash
     */
    std::span<const uint8_t> as_span() const
    {
        return {hash, hashSize};
    }

    /**
     * Return a string representation of the hash, in base-16, base-32
     * or base-64. By default, this is prefixed by the hash type
     * (e.g. "sha256:").
     */
    std::string to_string(HashFormat format = HashFormat::SRI, bool includeType = true) const;

    std::string gitRev() const
    {
        return to_string(HashFormat::Base16, false);
    }

    std::string gitShortRev() const
    {
        return std::string(gitRev(), 0, 7);
    }

    static Hash dummy;
};

/**
 * Helper that defaults empty hashes to the 0 hash.
 */
Hash newHashAllowEmpty(std::string_view hashStr, std::optional<HashType> ht);

/**
 * Compute the hash of the given string.
 */
Hash hashString(HashType ht, std::string_view s);

/**
 * Compute the hash of the given file.
 */
Hash hashFile(HashType ht, const Path & path);

/**
 * Compute the hash of the given path.  The hash is defined as
 * (essentially) hashString(ht, dumpPath(path)).
 */
typedef std::pair<Hash, uint64_t> HashResult;
HashResult hashPath(HashType ht, const PreparedDump & path);
inline HashResult hashPath(HashType ht, Path path)
{
    return hashPath(ht, *prepareDump(std::move(path)));
}

/**
 * Compress a hash to the specified number of bytes by cyclically
 * XORing bytes together.
 */
Hash compressHash(const Hash & hash, size_t newSize);

/**
 * Parse a string representing a hash type.
 */
HashType parseHashType(std::string_view s);

/**
 * Will return nothing on parse error
 */
std::optional<HashType> parseHashTypeOpt(std::string_view s);

/**
 * And the reverse.
 */
std::string_view printHashType(HashType ht);

struct AbstractHashSink : virtual Sink
{
    virtual HashResult finish() = 0;
};

class HashSink : public BufferedSink, public AbstractHashSink
{
private:
    HashType ht;
    detail::EvpMdCtxPtr ctx;
    uint64_t bytes;

public:
    HashSink(HashType ht);
    HashSink(const HashSink & h);
    void writeUnbuffered(std::string_view data) override;
    HashResult finish() override;
    HashResult currentHash();
};

inline HashResult hashSource(HashType ht, Source & source)
{
    HashSink h(ht);
    source.drainInto(h);
    return h.finish();
}
}
