#pragma once

#include "crypto/identity.hpp"
#include "crypto/signal_store.hpp"
#include "crypto/group_session.hpp"
#include "db/local_store.hpp"
#include "config.hpp"
#include "ircord.pb.h"

#include <signal_protocol.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ircord::crypto {

struct DecryptResult {
    std::string plaintext;
    std::string sender_id;
    bool        success = false;
};

// Pending key request callback: called when a KEY_BUNDLE arrives
using KeyBundleCallback = std::function<void(const std::string& recipient_id)>;

class CryptoEngine {
public:
    CryptoEngine();
    ~CryptoEngine();

    // Initialize: create signal context, load/generate identity.
    // passphrase: used to encrypt/decrypt the identity private key.
    // Returns false if initialization failed.
    bool init(db::LocalStore& store, const ClientConfig& cfg,
              const std::string& passphrase);

    bool ready() const { return identity_.loaded(); }

    // ── Registration ─────────────────────────────────────────────────────
    // Build a KeyUpload proto message with SPK + 100 OPKs.
    KeyUpload prepare_key_upload(int num_opks = 100);

    // ── Auth ─────────────────────────────────────────────────────────────
    std::vector<uint8_t> sign_challenge(const std::vector<uint8_t>& nonce,
                                         const std::string& user_id);

    // Public key (Ed25519, 32 bytes)
    std::vector<uint8_t> identity_pub() const;

    // Signed pre-key pub + sig for AuthResponse
    struct SpkInfo {
        std::vector<uint8_t> pub;
        std::vector<uint8_t> sig;
    };
    SpkInfo current_spk() const;

    // ── Encryption ────────────────────────────────────────────────────────
    // Encrypt a message for recipient (user_id or #channel).
    // Returns a ChatEnvelope proto ready to send. May be empty if session
    // not yet established (will call key_request_fn to trigger KEY_REQUEST).
    ChatEnvelope encrypt(const std::string& sender_id,
                          const std::string& recipient_id,
                          const std::string& plaintext,
                          std::function<void(const std::string&)> key_request_fn);

    // ── Decryption ────────────────────────────────────────────────────────
    DecryptResult decrypt(const ChatEnvelope& env);

    // ── Key bundle handling ────────────────────────────────────────────────
    // Called when KEY_BUNDLE arrives. Establishes X3DH session.
    void on_key_bundle(const KeyBundle& bundle, const std::string& recipient_id);

    // ── Safety number ─────────────────────────────────────────────────────
    std::string safety_number(const std::string& peer_id, db::LocalStore& store);

    const Identity& identity() const { return identity_; }

private:
    signal_context*               signal_ctx_  = nullptr;
    signal_protocol_store_context* store_ctx_  = nullptr;

    Identity                      identity_;
    std::unique_ptr<SignalStore>  signal_store_;
    std::unique_ptr<GroupSession> group_session_;

    // Current SPK (generated during init)
    Identity::SignedPreKey        spk_;
    uint32_t                      next_opk_id_ = 1;

    // Pending: recipients we're waiting for key bundles for
    std::unordered_map<std::string, std::string> pending_plaintexts_;

    db::LocalStore* local_store_ = nullptr;

    void setup_signal_crypto(signal_context* ctx);
};

} // namespace ircord::crypto
