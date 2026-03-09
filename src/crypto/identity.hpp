#pragma once
#include "db/local_store.hpp"
#include <array>
#include <string>
#include <vector>
#include <cstdint>

namespace ircord::crypto {

// Ed25519 key pair (libsodium)
struct Ed25519KeyPair {
    std::array<uint8_t, 32> pub;   // crypto_sign_PUBLICKEYBYTES = 32
    std::array<uint8_t, 64> priv;  // crypto_sign_SECRETKEYBYTES = 64
};

// X25519 key pair (for Diffie-Hellman in Signal Protocol)
struct X25519KeyPair {
    std::array<uint8_t, 32> pub;
    std::array<uint8_t, 32> priv;
};

class Identity {
public:
    // Generate a new identity and save it to the store (encrypted with passphrase).
    bool generate_and_save(const std::string& user_id,
                           const std::string& passphrase,
                           db::LocalStore& store);

    // Load existing identity from store, decrypting with passphrase.
    bool load(const std::string& user_id,
              const std::string& passphrase,
              db::LocalStore& store);

    // Sign challenge: Ed25519 sign(nonce || user_id)
    // Returns 64-byte signature.
    std::vector<uint8_t> sign_challenge(const std::vector<uint8_t>& nonce,
                                        const std::string& user_id) const;

    // Generate a signed pre-key (X25519), sign with identity key.
    struct SignedPreKey {
        uint32_t             id;
        X25519KeyPair        key_pair;
        std::vector<uint8_t> signature;  // Ed25519 sign(SPK pub)
    };
    SignedPreKey generate_signed_prekey(uint32_t id) const;

    // Generate one-time pre-keys
    std::vector<std::pair<uint32_t, X25519KeyPair>>
    generate_one_time_prekeys(uint32_t start_id, int count) const;

    const Ed25519KeyPair& ed25519() const { return ed25519_; }

    bool loaded() const { return loaded_; }

    // Human-readable safety number (60 digits) comparing two identity keys
    static std::string safety_number(const std::array<uint8_t, 32>& our_pub,
                                     const std::array<uint8_t, 32>& their_pub);

private:
    Ed25519KeyPair ed25519_{};
    bool           loaded_ = false;

    // Encrypt private key with Argon2id + XChaCha20-Poly1305
    static std::vector<uint8_t> encrypt_priv(
        const std::array<uint8_t, 64>& priv_key,
        const std::string& passphrase,
        std::vector<uint8_t>& salt_out);

    static bool decrypt_priv(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& salt,
        const std::string& passphrase,
        std::array<uint8_t, 64>& priv_out);
};

} // namespace ircord::crypto
