#include "crypto/identity.hpp"
#include <sodium.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ircord::crypto {

// ── Key encryption (Argon2id + XChaCha20-Poly1305) ───────────────────────────

std::vector<uint8_t> Identity::encrypt_priv(
    const std::array<uint8_t, 64>& priv_key,
    const std::string& passphrase,
    std::vector<uint8_t>& salt_out)
{
    // Generate salt
    salt_out.resize(crypto_pwhash_SALTBYTES);
    randombytes_buf(salt_out.data(), salt_out.size());

    // Derive key with Argon2id
    std::array<uint8_t, crypto_secretstream_xchacha20poly1305_KEYBYTES> key{};
    if (crypto_pwhash(
            key.data(), key.size(),
            passphrase.data(), passphrase.size(),
            salt_out.data(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE,
            crypto_pwhash_ALG_ARGON2ID13) != 0) {
        throw std::runtime_error("Argon2id key derivation failed (out of memory?)");
    }

    // Encrypt
    std::vector<uint8_t> ciphertext(
        crypto_secretbox_NONCEBYTES +
        priv_key.size() + crypto_secretbox_MACBYTES);

    uint8_t* nonce = ciphertext.data();
    randombytes_buf(nonce, crypto_secretbox_NONCEBYTES);

    if (crypto_secretbox_easy(
            ciphertext.data() + crypto_secretbox_NONCEBYTES,
            priv_key.data(), priv_key.size(),
            nonce, key.data()) != 0) {
        throw std::runtime_error("Encryption of private key failed");
    }

    return ciphertext;
}

bool Identity::decrypt_priv(
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& salt,
    const std::string& passphrase,
    std::array<uint8_t, 64>& priv_out)
{
    if (ciphertext.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) {
        return false;
    }

    std::array<uint8_t, crypto_secretstream_xchacha20poly1305_KEYBYTES> key{};
    if (crypto_pwhash(
            key.data(), key.size(),
            passphrase.data(), passphrase.size(),
            salt.data(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE,
            crypto_pwhash_ALG_ARGON2ID13) != 0) {
        return false;
    }

    const uint8_t* nonce      = ciphertext.data();
    const uint8_t* ctext      = ciphertext.data() + crypto_secretbox_NONCEBYTES;
    size_t         ctext_size = ciphertext.size() - crypto_secretbox_NONCEBYTES;

    if (crypto_secretbox_open_easy(
            priv_out.data(), ctext, ctext_size, nonce, key.data()) != 0) {
        return false;
    }

    return true;
}

// ── Identity generation/loading ───────────────────────────────────────────────

bool Identity::generate_and_save(const std::string& user_id,
                                   const std::string& passphrase,
                                   db::LocalStore& store) {
    if (crypto_sign_keypair(ed25519_.pub.data(), ed25519_.priv.data()) != 0) {
        spdlog::error("Failed to generate Ed25519 key pair");
        return false;
    }

    std::vector<uint8_t> salt;
    std::vector<uint8_t> enc_priv;
    try {
        enc_priv = encrypt_priv(ed25519_.priv, passphrase, salt);
    } catch (const std::exception& e) {
        spdlog::error("Identity encryption error: {}", e.what());
        return false;
    }

    db::LocalStore::IdentityRow row;
    row.user_id            = user_id;
    row.identity_pub       = {ed25519_.pub.begin(), ed25519_.pub.end()};
    row.identity_priv_enc  = enc_priv;
    row.salt               = salt;
    row.created_at         = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    store.save_identity(row);
    loaded_ = true;
    spdlog::info("Generated new identity for '{}'", user_id);
    return true;
}

bool Identity::load(const std::string& user_id,
                     const std::string& passphrase,
                     db::LocalStore& store) {
    auto row = store.load_identity(user_id);
    if (!row) {
        spdlog::info("No stored identity for '{}'", user_id);
        return false;
    }

    if (row->identity_pub.size() != 32) {
        spdlog::error("Stored identity pub key has wrong size");
        return false;
    }
    std::copy(row->identity_pub.begin(), row->identity_pub.end(), ed25519_.pub.begin());

    if (!decrypt_priv(row->identity_priv_enc, row->salt, passphrase, ed25519_.priv)) {
        spdlog::error("Failed to decrypt identity private key (wrong passphrase?)");
        return false;
    }

    loaded_ = true;
    spdlog::info("Loaded identity for '{}'", user_id);
    return true;
}

// ── Signing ───────────────────────────────────────────────────────────────────

std::vector<uint8_t> Identity::sign_challenge(const std::vector<uint8_t>& nonce,
                                                const std::string& user_id) const {
    if (!loaded_) throw std::runtime_error("Identity not loaded");

    // message = nonce || user_id
    std::vector<uint8_t> message;
    message.insert(message.end(), nonce.begin(), nonce.end());
    message.insert(message.end(), user_id.begin(), user_id.end());

    std::vector<uint8_t> sig(crypto_sign_BYTES);
    unsigned long long sig_len = 0;
    if (crypto_sign_detached(sig.data(), &sig_len,
                             message.data(), message.size(),
                             ed25519_.priv.data()) != 0) {
        throw std::runtime_error("Ed25519 sign failed");
    }
    sig.resize(sig_len);
    return sig;
}

// ── Signed pre-key generation ─────────────────────────────────────────────────

Identity::SignedPreKey Identity::generate_signed_prekey(uint32_t id) const {
    if (!loaded_) throw std::runtime_error("Identity not loaded");

    SignedPreKey spk;
    spk.id = id;

    // Generate X25519 DH key pair via libsodium crypto_box_keypair
    // (Curve25519 = X25519)
    if (crypto_box_keypair(spk.key_pair.pub.data(), spk.key_pair.priv.data()) != 0) {
        throw std::runtime_error("X25519 key generation failed");
    }

    // Sign the public key with our Ed25519 identity key
    spk.signature.resize(crypto_sign_BYTES);
    unsigned long long sig_len = 0;
    if (crypto_sign_detached(spk.signature.data(), &sig_len,
                             spk.key_pair.pub.data(), spk.key_pair.pub.size(),
                             ed25519_.priv.data()) != 0) {
        throw std::runtime_error("SPK signing failed");
    }
    spk.signature.resize(sig_len);
    return spk;
}

// ── One-time pre-key generation ───────────────────────────────────────────────

std::vector<std::pair<uint32_t, X25519KeyPair>>
Identity::generate_one_time_prekeys(uint32_t start_id, int count) const {
    std::vector<std::pair<uint32_t, X25519KeyPair>> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        X25519KeyPair kp{};
        crypto_box_keypair(kp.pub.data(), kp.priv.data());
        result.emplace_back(start_id + i, kp);
    }
    return result;
}

// ── Safety number ─────────────────────────────────────────────────────────────

std::string Identity::safety_number(const std::array<uint8_t, 32>& our_pub,
                                     const std::array<uint8_t, 32>& their_pub) {
    // Hash both keys together (sorted for symmetry) to produce 30 5-digit numbers
    std::array<uint8_t, 64> combined{};

    // Sort deterministically: smaller pub first
    if (our_pub < their_pub) {
        std::copy(our_pub.begin(),   our_pub.end(),   combined.begin());
        std::copy(their_pub.begin(), their_pub.end(), combined.begin() + 32);
    } else {
        std::copy(their_pub.begin(), their_pub.end(), combined.begin());
        std::copy(our_pub.begin(),   our_pub.end(),   combined.begin() + 32);
    }

    std::array<uint8_t, crypto_generichash_BYTES> hash{};
    crypto_generichash(hash.data(), hash.size(), combined.data(), combined.size(), nullptr, 0);

    // Format as 12 groups of 5 digits
    std::ostringstream oss;
    for (int i = 0; i < 12; ++i) {
        uint32_t val = 0;
        for (int j = 0; j < 3; ++j) {
            val = (val << 8) | hash[(i * 3 + j) % hash.size()];
        }
        val %= 100000;
        if (i > 0) oss << ' ';
        oss << std::setw(5) << std::setfill('0') << val;
    }
    return oss.str();
}

} // namespace ircord::crypto
