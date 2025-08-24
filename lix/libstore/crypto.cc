#include "lix/libutil/charptr-cast.hh"
#include "lix/libstore/crypto.hh"
#include "lix/libutil/file-system.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/strings.hh"

#include <openssl/err.h>

namespace nix {

constexpr size_t ED25519_KEY_BYTES = 32;
constexpr size_t ED25519_SIGNATURE_BYTES = 64;
constexpr size_t MAX_ERROR_MESSAGE_LENGTH = 256;

using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype([](auto ctx) { EVP_PKEY_CTX_free(ctx); })>;
using EvpSignaturePtr = std::unique_ptr<EVP_SIGNATURE, decltype([](auto alg) { EVP_SIGNATURE_free(alg); })>;

static std::pair<std::string_view, std::string> split(std::string_view s)
{
    size_t colon = s.find(':');
    if (colon == std::string::npos || colon == 0)
        return {"", ""};
    return {s.substr(0, colon), base64Decode(s.substr(colon + 1))};
}

std::string openssl_error()
{
    auto error = ERR_get_error();
    char buf[MAX_ERROR_MESSAGE_LENGTH];
    ERR_error_string_n(error, buf, MAX_ERROR_MESSAGE_LENGTH);
    return buf;
}

SecretKey::SecretKey(std::string name, EvpPkeyPtr pkey)
    : name(std::move(name)), pkey(std::move(pkey))
{}

SecretKey::~SecretKey() = default;

std::string SecretKey::signDetached(std::string_view data) const
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey.get(), nullptr);
    if (!ctx)
        throw Error("signing failed: %s", openssl_error());
    EvpPkeyCtxPtr pctx(ctx);
    EVP_SIGNATURE *alg = EVP_SIGNATURE_fetch(nullptr, "ED25519", nullptr);
    if (!alg)
        throw Error("signing failed: %s", openssl_error());
    EvpSignaturePtr palg(alg);
    if (EVP_PKEY_sign_message_init(pctx.get(), palg.get(), nullptr) != 1)
        throw Error("signing failed: %s", openssl_error());

    unsigned char sig[ED25519_SIGNATURE_BYTES];
    size_t sig_bytes = ED25519_SIGNATURE_BYTES;
    if (EVP_PKEY_sign(
        pctx.get(),
        sig,
        &sig_bytes,
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
        charptr_cast<const unsigned char *>(data.data()),
        data.size()
    ) != 1)
        throw Error("signing failed: %s", openssl_error());
    assert(sig_bytes == ED25519_SIGNATURE_BYTES);

    return fmt("%s:%s", name, base64Encode({charptr_cast<char *>(sig), ED25519_SIGNATURE_BYTES}));
}

PublicKey SecretKey::toPublicKey() const
{
    unsigned char raw[ED25519_KEY_BYTES];
    size_t key_bytes = ED25519_KEY_BYTES;
    assert(EVP_PKEY_get_raw_public_key(pkey.get(), raw, &key_bytes) == 1);
    assert(key_bytes == ED25519_KEY_BYTES);
    return PublicKey::fromRaw(name, {charptr_cast<char *>(raw), ED25519_KEY_BYTES});
}

std::string SecretKey::to_string() const
{
    // For compatibility reasons, the public key is included, even though it is redundant.
    unsigned char keys[2 * ED25519_KEY_BYTES];
    size_t key_bytes = ED25519_KEY_BYTES;
    assert(EVP_PKEY_get_raw_private_key(pkey.get(), keys, &key_bytes) == 1);
    assert(key_bytes == ED25519_KEY_BYTES);
    assert(EVP_PKEY_get_raw_public_key(pkey.get(), keys + ED25519_KEY_BYTES, &key_bytes) == 1);
    assert(key_bytes == ED25519_KEY_BYTES);
    return fmt("%s:%s", name, base64Encode({charptr_cast<char *>(keys), 2 * ED25519_KEY_BYTES}));
}

SecretKey SecretKey::generate(std::string_view name)
{
    EVP_PKEY *key = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
    if (!key)
        throw Error("key generation failed: %s", openssl_error());
    EvpPkeyPtr pkey(key);

    return SecretKey(std::string(name), std::move(pkey));
}

SecretKey SecretKey::parse(std::string_view s)
{
    auto [name, raw_key] = split(s);

    // For compatibility reasons, the public key is included, even though it is redundant.
    if (raw_key.size() != 2 * ED25519_KEY_BYTES)
        throw Error("secret key is not valid");

    EVP_PKEY *key = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519,
        nullptr,
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
        charptr_cast<const unsigned char *>(raw_key.data()),
        ED25519_KEY_BYTES
    );
    if (!key)
        throw Error("secret key is not valid: %s", openssl_error());
    EvpPkeyPtr pkey(key);

    // Verify that the redundant copy of the public key is correct.
    unsigned char pk[ED25519_KEY_BYTES];
    size_t key_bytes = ED25519_KEY_BYTES;
    assert(EVP_PKEY_get_raw_public_key(pkey.get(), pk, &key_bytes) == 1);
    assert(key_bytes == ED25519_KEY_BYTES);
    if (memcmp(pk, raw_key.data() + ED25519_KEY_BYTES, ED25519_KEY_BYTES) != 0)
        throw Error("secret key is not valid");

    return SecretKey(std::string(name), std::move(pkey));
}

PublicKey::PublicKey(std::string name, EvpPkeyPtr pkey)
    : name(std::move(name)), pkey(std::move(pkey))
{}

PublicKey::~PublicKey() = default;

bool PublicKey::verifyDetached(std::string_view data, std::string_view sig) const
{
    if (sig.size() != ED25519_SIGNATURE_BYTES)
        throw Error("signature is not valid");

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey.get(), nullptr);
    if (!ctx)
        throw Error("signature verification failed: %s", openssl_error());
    EvpPkeyCtxPtr pctx(ctx);
    EVP_SIGNATURE *alg = EVP_SIGNATURE_fetch(nullptr, "ED25519", nullptr);
    if (!alg)
        throw Error("signature verification failed: %s", openssl_error());
    EvpSignaturePtr palg(alg);
    if (EVP_PKEY_verify_message_init(pctx.get(), palg.get(), nullptr) != 1)
        throw Error("signature verification failed: %s", openssl_error());

    int result = EVP_PKEY_verify(
        pctx.get(),
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
        charptr_cast<const unsigned char *>(sig.data()),
        ED25519_SIGNATURE_BYTES,
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
        charptr_cast<const unsigned char *>(data.data()),
        data.size()
    );
    switch (result) {
    case 1:
        // success
        return true;
    case 0:
        // signature did not verify
        return false;
    default:
        // negative return value indicates more serious error
        throw Error("signature verification failed: %s", openssl_error());
    }
}

std::string PublicKey::to_string() const
{
    unsigned char pk[ED25519_KEY_BYTES];
    size_t key_bytes = ED25519_KEY_BYTES;
    assert(EVP_PKEY_get_raw_public_key(pkey.get(), pk, &key_bytes) == 1);
    assert(key_bytes == ED25519_KEY_BYTES);
    return fmt("%s:%s", name, base64Encode({charptr_cast<char *>(pk), ED25519_KEY_BYTES}));
}

PublicKey PublicKey::fromRaw(std::string_view name, std::string_view raw)
{
    if (raw.size() != ED25519_KEY_BYTES)
        throw Error("public key is not valid");

    EVP_PKEY *key = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519,
        nullptr,
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
        charptr_cast<const unsigned char *>(raw.data()),
        ED25519_KEY_BYTES
    );
    if (!key)
        throw Error("public key is not valid: %s", openssl_error());
    EvpPkeyPtr pkey(key);

    return PublicKey(std::string(name), std::move(pkey));
}

PublicKey PublicKey::parse(std::string_view s)
{
    auto [name, raw] = split(s);
    return PublicKey::fromRaw(name, raw);
}

bool verifyDetached(const std::string & data, const std::string & sig,
    const PublicKeys & publicKeys)
{
    auto [name, sig2] = split(sig);

    auto key = publicKeys.find(std::string(name));
    if (key == publicKeys.end()) return false;

    return key->second.verifyDetached(data, sig2);
}

PublicKeys getDefaultPublicKeys()
{
    PublicKeys publicKeys;

    // FIXME: filter duplicates

    for (auto s : settings.trustedPublicKeys.get()) {
        auto key = PublicKey::parse(s);
        auto name = key.name;
        publicKeys.emplace(name, std::move(key));
    }

    for (auto secretKeyFile : settings.secretKeyFiles.get()) {
        try {
            auto secretKey = SecretKey::parse(readFile(secretKeyFile));
            publicKeys.emplace(secretKey.name, secretKey.toPublicKey());
        } catch (SysError & e) {
            /* Ignore unreadable key files. That's normal in a
               multi-user installation. */
        }
    }

    return publicKeys;
}

}
