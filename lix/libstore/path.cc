#include "lix/libstore/store-api.hh"

namespace nix {

static void checkName(std::string_view path, std::string_view name)
{
    if (name.empty())
        throw BadStorePath("store path '%s' has an empty name", path);
    if (name.size() > StorePath::MAX_PATH_LEN) {
        throw BadStorePath(
            "store path '%s' has a name longer than %d characters", path, StorePath::MAX_PATH_LEN
        );
    }
    // See nameRegexStr for the definition
    if (name[0] == '.') {
        // check against "." and "..", followed by end or dash
        if (name.size() == 1)
            throw BadStorePath("store path '%s' has invalid name '%s'", path, name);
        if (name[1] == '-')
            throw BadStorePath("store path '%s' has invalid name '%s': first dash-separated component must not be '%s'", path, name, ".");
        if (name[1] == '.') {
            if (name.size() == 2)
                throw BadStorePath("store path '%s' has invalid name '%s'", path, name);
            if (name[2] == '-')
                throw BadStorePath("store path '%s' has invalid name '%s': first dash-separated component must not be '%s'", path, name, "..");
        }
    }
    for (auto c : name)
        if (!((c >= '0' && c <= '9')
                || (c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z')
                || c == '+' || c == '-' || c == '.' || c == '_' || c == '?' || c == '='))
            throw BadStorePath("store path '%s' contains illegal character '%s'", path, c);
}

StorePath::StorePath(std::string_view _baseName)
    : baseName(_baseName)
{
    if (baseName.size() < HASH_PART_LEN + 1) {
        throw BadStorePath("'%s' is too short to be a valid store path", baseName);
    }
    for (auto c : hashPart())
        if (c == 'e' || c == 'o' || c == 'u' || c == 't'
            || !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')))
            throw BadStorePath("store path '%s' contains illegal base-32 character '%s'", baseName, c);
    checkName(baseName, name());
}

StorePath::StorePath(const Hash & hash, std::string_view _name)
    : baseName((hash.to_string(Base::Base32, false) + "-").append(std::string(_name)))
{
    assert(hash.base32Len() == HASH_PART_LEN);
    checkName(baseName, name());
}

bool StorePath::isDerivation() const
{
    return name().ends_with(drvExtension);
}

StorePath StorePath::dummy("ffffffffffffffffffffffffffffffff-x");

StorePath Store::parseStorePath(std::string_view path) const
{
    auto p = canonPath(std::string(path));
    if (dirOf(p) != config().storeDir)
        throw BadStorePath("path '%s' is not in the Nix store", p);
    return StorePath(baseNameOf(p));
}

std::optional<StorePath> Store::maybeParseStorePath(std::string_view path) const
{
    // If it's not an absolute path, or if the dirname of the path isn't /nix/store
    // (or whatever our storeDir is), then it can't be a store path.
    if (path.size() == 0 || path[0] != '/' || dirOf(canonPath(path)) != config().storeDir) {
        return std::nullopt;
    }
    try {
        return parseStorePath(path);
    } catch (Error &) {
        return {};
    }
}

bool Store::isStorePath(std::string_view path) const
{
    return (bool) maybeParseStorePath(path);
}

StorePathSet Store::parseStorePathSet(const PathSet & paths) const
{
    StorePathSet res;
    for (auto & i : paths) res.insert(parseStorePath(i));
    return res;
}

std::string Store::printStorePath(const StorePath & path) const
{
    return (config().storeDir + "/").append(path.to_string());
}

PathSet Store::printStorePathSet(const StorePathSet & paths) const
{
    PathSet res;
    for (auto & i : paths) res.insert(printStorePath(i));
    return res;
}

}

std::size_t std::hash<nix::StorePath>::operator()(const nix::StorePath & path) const noexcept
{
    // It's already a cryptographic hash of 160 bits (assuming that nobody gives us bogus ones...), so just parse it.
    auto h = nix::Hash::parseNonSRIUnprefixed(path.hashPart(), nix::HashType::SHA1);
    // This need not be stable across machines, so bit casting the start of it is fine.
    size_t r;
    memcpy(&r, h.hash, sizeof(r));
    return r;
}
