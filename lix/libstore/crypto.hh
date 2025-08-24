#pragma once
///@file


#include <map>
#include <memory>
#include <string>

#include <openssl/evp.h>

namespace nix {

using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype([](auto key) { EVP_PKEY_free(key); })>;

struct PublicKey;

struct SecretKey
{
    std::string name;
    EvpPkeyPtr pkey;

    SecretKey(std::string name, EvpPkeyPtr pkey);
    SecretKey(SecretKey &&) = default;
    ~SecretKey();

    /**
     * Return a detached signature of the given string.
     */
    std::string signDetached(std::string_view s) const;

    PublicKey toPublicKey() const;

    std::string to_string() const;

    static SecretKey generate(std::string_view name);

    /**
     * Parse a secret key in the format `<name>:<key-in-base64>`.
     * For backwards compatibility, the key must be the concatenation of the secret and public key.
     */
    static SecretKey parse(std::string_view s);
};

struct PublicKey
{
    std::string name;
    EvpPkeyPtr pkey;

    PublicKey(std::string name, EvpPkeyPtr pkey);
    PublicKey(PublicKey &&) = default;
    ~PublicKey();

    /**
     * Check whether a detached signature is valid.
     */
    bool verifyDetached(std::string_view data, std::string_view sig) const;

    std::string to_string() const;

    static PublicKey fromRaw(std::string_view name, std::string_view raw);

    /**
     * Parse a public key in the format `<name>:<key-in-base64>`.
     */
    static PublicKey parse(std::string_view data);
};

typedef std::map<std::string, PublicKey> PublicKeys;

/**
 * @return true iff ‘sig’ is a correct signature over ‘data’ using one
 * of the given public keys.
 */
bool verifyDetached(const std::string & data, const std::string & sig,
    const PublicKeys & publicKeys);

PublicKeys getDefaultPublicKeys();

}
