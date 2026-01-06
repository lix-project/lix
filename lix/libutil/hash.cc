#include <cstring>

#include <openssl/evp.h>

#include "lix/libutil/args.hh"
#include "lix/libutil/hash.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/charptr-cast.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/split.hh"
#include "lix/libutil/strings.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {

const std::set<std::string> hashTypes = {"md5", "sha1", "sha256", "sha512"};

const std::string base16Chars = "0123456789abcdef";


static std::string printHash16(const Hash & hash)
{
    std::string buf;
    buf.reserve(hash.hashSize * 2);
    for (unsigned int i = 0; i < hash.hashSize; i++) {
        buf.push_back(base16Chars[hash.hash[i] >> 4]);
        buf.push_back(base16Chars[hash.hash[i] & 0x0f]);
    }
    return buf;
}

std::string Hash::to_string(HashFormat format, bool includeType) const
{
    std::string s;
    if (format == HashFormat::SRI || includeType) {
        s += printHashType(type);
        s += format == HashFormat::SRI ? '-' : ':';
    }
    switch (format) {
    case HashFormat::Base16:
        s += printHash16(*this);
        break;
    case HashFormat::Base32:
        s += base32EncodeStr(std::string_view(charptr_cast<const char *>(hash), hashSize));
        break;
    case HashFormat::Base64:
    case HashFormat::SRI:
        s += base64Encode(std::string_view(charptr_cast<const char *>(hash), hashSize));
        break;
    }
    return s;
}

Hash Hash::dummy(HashType::SHA256);

Hash Hash::parseSRI(std::string_view original) {
    auto rest = original;

    // Parse the has type before the separater, if there was one.
    auto hashRaw = splitPrefixTo(rest, '-');
    if (!hashRaw)
        throw BadHash("hash '%s' is not SRI", original);
    HashType parsedType = parseHashType(*hashRaw);

    return Hash(rest, parsedType, true);
}

// Mutates the string to eliminate the prefixes when found
static std::pair<std::optional<HashType>, bool> getParsedTypeAndSRI(std::string_view & rest)
{
    bool isSRI = false;

    // Parse the hash type before the separator, if there was one.
    std::optional<HashType> optParsedType;
    {
        auto hashRaw = splitPrefixTo(rest, ':');

        if (!hashRaw) {
            hashRaw = splitPrefixTo(rest, '-');
            if (hashRaw)
                isSRI = true;
        }
        if (hashRaw)
            optParsedType = parseHashType(*hashRaw);
    }

    return {optParsedType, isSRI};
}

Hash Hash::parseAnyPrefixed(std::string_view original)
{
    auto rest = original;
    auto [optParsedType, isSRI] = getParsedTypeAndSRI(rest);

    // Either the string or user must provide the type, if they both do they
    // must agree.
    if (!optParsedType)
        throw BadHash("hash '%s' does not include a type", rest);

    return Hash(rest, *optParsedType, isSRI);
}

Hash Hash::parseAny(std::string_view original, std::optional<HashType> optType)
{
    auto rest = original;
    auto [optParsedType, isSRI] = getParsedTypeAndSRI(rest);

    // Either the string or user must provide the type, if they both do they
    // must agree.
    if (!optParsedType && !optType)
        throw BadHash("hash '%s' does not include a type, nor is the type otherwise known from context", rest);
    else if (optParsedType && optType && *optParsedType != *optType)
        throw BadHash("hash '%s' should have type '%s'", original, printHashType(*optType));

    HashType hashType = optParsedType ? *optParsedType : *optType;
    return Hash(rest, hashType, isSRI);
}

Hash Hash::parseNonSRIUnprefixed(std::string_view s, HashType type)
{
    return Hash(s, type, false);
}

Hash::Hash(std::string_view rest, HashType type, bool isSRI)
    : Hash(type)
{
    if (!isSRI && rest.size() == base16Len()) {

        auto parseHexDigit = [&](char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            throw BadHash("invalid base-16 hash '%s'", rest);
        };

        for (unsigned int i = 0; i < hashSize; i++) {
            const size_t j = i << 1;
            hash[i] = parseHexDigit(rest[j]) << 4 | parseHexDigit(rest[j + 1]);
        }
    }

    else if (!isSRI && rest.size() == base32Len()) {
        auto d = base32Decode(rest);
        memcpy(hash, d.data(), hashSize);
    }

    else if (isSRI || rest.size() == base64Len()) {
        auto d = base64Decode(rest);
        if (d.size() != hashSize)
            throw BadHash("invalid %s hash '%s'", isSRI ? "SRI" : "base-64", rest);
        assert(hashSize);
        memcpy(hash, d.data(), hashSize);
    }

    else
        throw BadHash("hash '%s' has wrong length for hash type '%s'", rest, printHashType(this->type));
}

Hash newHashAllowEmpty(std::string_view hashStr, std::optional<HashType> ht)
{
    if (hashStr.empty()) {
        if (!ht)
            throw BadHash("empty hash requires explicit hash type");
        Hash h(*ht);
        printTaggedWarning("found empty hash, assuming '%s'", h.to_string(HashFormat::SRI, true));
        return h;
    } else
        return Hash::parseAny(hashStr, ht);
}


static detail::EvpMdCtxPtr start(HashType ht)
{
    detail::EvpMdCtxPtr ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx) {
        throw Error("failed to create message digest context");
    }

    int ok;
    switch (ht) {
    case HashType::MD5: ok = EVP_DigestInit_ex(ctx.get(), EVP_md5(), NULL); break;
    case HashType::SHA1: ok = EVP_DigestInit_ex(ctx.get(), EVP_sha1(), NULL); break;
    case HashType::SHA256: ok = EVP_DigestInit_ex(ctx.get(), EVP_sha256(), NULL); break;
    case HashType::SHA512: ok = EVP_DigestInit_ex(ctx.get(), EVP_sha512(), NULL); break;
    }
    if (!ok) {
        throw Error("failed to initialize message digest");
    }

    return ctx;
}


static void update(detail::EvpMdCtxPtr & ctx, std::string_view data)
{
    if (!EVP_DigestUpdate(ctx.get(), data.data(), data.size())) {
        throw Error("failed to update message digest with %zu bytes", data.size());
    }
}

static void finish(detail::EvpMdCtxPtr & ctx, unsigned char * hash)
{
    if (!EVP_DigestFinal_ex(ctx.get(), hash, NULL)) {
        throw Error("failed to finalize message digest");
    }
}

Hash hashString(HashType ht, std::string_view s)
{
    Hash hash(ht);
    detail::EvpMdCtxPtr ctx = start(ht);
    update(ctx, s);
    finish(ctx, hash.hash);
    return hash;
}

Hash hashFile(HashType ht, const Path & path)
{
    HashSink sink(ht);
    sink << readFileSource(path);
    return sink.finish().first;
}

HashSink::HashSink(HashType ht) : ht(ht), ctx(start(ht)), bytes(0) {}

void HashSink::writeUnbuffered(std::string_view data)
{
    bytes += data.size();
    update(ctx, data);
}

HashResult HashSink::finish()
{
    flush();
    Hash hash(ht);
    nix::finish(ctx, hash.hash);
    return HashResult(hash, bytes);
}

HashResult HashSink::currentHash()
{
    flush();
    detail::EvpMdCtxPtr ctx2 = start(ht);
    if (!EVP_MD_CTX_copy(ctx2.get(), ctx.get())) {
        throw Error("failed to copy message digest");
    }
    Hash hash(ht);
    nix::finish(ctx2, hash.hash);
    return HashResult(hash, bytes);
}

HashResult hashPath(HashType ht, const PreparedDump & path)
{
    HashSink sink(ht);
    sink << path.dump();
    return sink.finish();
}

Hash compressHash(const Hash & hash, size_t newSize)
{
    Hash h(newSize, hash.type);

    for (const auto [idx, c] : enumerate(hash.as_span())) {
        h.hash[idx % newSize] ^= c;
    }

    return h;
}

std::optional<HashType> parseHashTypeOpt(std::string_view s)
{
    if (s == "md5") {
        return HashType::MD5;
    }
    if (s == "sha1") {
        return HashType::SHA1;
    }
    if (s == "sha256") {
        return HashType::SHA256;
    }
    if (s == "sha512") {
        return HashType::SHA512;
    }

    return std::nullopt;
}

HashType parseHashType(std::string_view s)
{
    if (auto opt_h = parseHashTypeOpt(s)) {
        return *opt_h;
    }

    throw UsageError("unknown hash algorithm '%1%'", s);
}

std::string_view printHashType(HashType type)
{
    switch (type) {
    case HashType::MD5:
        return "md5";
    case HashType::SHA1:
        return "sha1";
    case HashType::SHA256:
        return "sha256";
    case HashType::SHA512:
        return "sha512";
    default:
        // illegal hash type enum value internally, as opposed to external input
        // which should be validated with nice error message.
        assert(false);
    }
}

}
