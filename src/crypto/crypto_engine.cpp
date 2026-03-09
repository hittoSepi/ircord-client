#include "crypto/crypto_engine.hpp"
#include <sodium.h>
#include <signal_protocol.h>
#include <curve.h>
#include <session_builder.h>
#include <session_cipher.h>
#include <key_helper.h>
#include <session_pre_key.h>
#include <protocol.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <chrono>

// libsignal-protocol-c requires a crypto provider backed by libsodium
extern "C" {
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
}

namespace ircord::crypto {

// ── libsignal-protocol-c crypto provider (OpenSSL-backed) ────────────────────

static int signal_random_bytes(uint8_t* data, size_t len, void*) {
    randombytes_buf(data, len);
    return SG_SUCCESS;
}

static int signal_hmac_sha256_init(void** ctx, const uint8_t* key, size_t key_len, void*) {
    HMAC_CTX* hmac = HMAC_CTX_new();
    if (!hmac) return SG_ERR_NOMEM;
    HMAC_Init_ex(hmac, key, static_cast<int>(key_len), EVP_sha256(), nullptr);
    *ctx = hmac;
    return SG_SUCCESS;
}

static int signal_hmac_sha256_update(void* ctx, const uint8_t* data, size_t len, void*) {
    HMAC_Update(static_cast<HMAC_CTX*>(ctx), data, len);
    return SG_SUCCESS;
}

static int signal_hmac_sha256_final(void* ctx, signal_buffer** out, void*) {
    auto* hmac = static_cast<HMAC_CTX*>(ctx);
    uint8_t buf[32]; unsigned int len = 32;
    HMAC_Final(hmac, buf, &len);
    *out = signal_buffer_create(buf, len);
    return *out ? SG_SUCCESS : SG_ERR_NOMEM;
}

static void signal_hmac_sha256_cleanup(void* ctx, void*) {
    HMAC_CTX_free(static_cast<HMAC_CTX*>(ctx));
}

static int signal_sha512_digest_init(void** ctx, void*) {
    SHA512_CTX* sha = new SHA512_CTX;
    SHA512_Init(sha);
    *ctx = sha;
    return SG_SUCCESS;
}

static int signal_sha512_digest_update(void* ctx, const uint8_t* data, size_t len, void*) {
    SHA512_Update(static_cast<SHA512_CTX*>(ctx), data, len);
    return SG_SUCCESS;
}

static int signal_sha512_digest_final(void* ctx, signal_buffer** out, void*) {
    auto* sha = static_cast<SHA512_CTX*>(ctx);
    uint8_t buf[64];
    SHA512_Final(buf, sha);
    *out = signal_buffer_create(buf, 64);
    return *out ? SG_SUCCESS : SG_ERR_NOMEM;
}

static void signal_sha512_digest_cleanup(void* ctx, void*) {
    delete static_cast<SHA512_CTX*>(ctx);
}

// AES-CBC encrypt/decrypt via OpenSSL
static int signal_encrypt(signal_buffer** out,
                           int cipher,
                           const uint8_t* key, size_t key_len,
                           const uint8_t* iv, size_t iv_len,
                           const uint8_t* data, size_t data_len,
                           void*)
{
    const EVP_CIPHER* evp = nullptr;
    switch (cipher) {
    case SG_CIPHER_AES_CBC_PKCS5:
        evp = (key_len == 16) ? EVP_aes_128_cbc() : EVP_aes_256_cbc(); break;
    case SG_CIPHER_AES_CTR_NOPADDING:
        evp = (key_len == 16) ? EVP_aes_128_ctr() : EVP_aes_256_ctr(); break;
    default: return SG_ERR_UNKNOWN;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, evp, nullptr, key, iv);
    if (cipher == SG_CIPHER_AES_CTR_NOPADDING) EVP_CIPHER_CTX_set_padding(ctx, 0);

    std::vector<uint8_t> buf(data_len + EVP_CIPHER_block_size(evp));
    int len1 = 0, len2 = 0;
    EVP_EncryptUpdate(ctx, buf.data(), &len1, data, static_cast<int>(data_len));
    EVP_EncryptFinal_ex(ctx, buf.data() + len1, &len2);
    EVP_CIPHER_CTX_free(ctx);

    *out = signal_buffer_create(buf.data(), len1 + len2);
    return *out ? SG_SUCCESS : SG_ERR_NOMEM;
}

static int signal_decrypt(signal_buffer** out,
                           int cipher,
                           const uint8_t* key, size_t key_len,
                           const uint8_t* iv, size_t iv_len,
                           const uint8_t* data, size_t data_len,
                           void*)
{
    const EVP_CIPHER* evp = nullptr;
    switch (cipher) {
    case SG_CIPHER_AES_CBC_PKCS5:
        evp = (key_len == 16) ? EVP_aes_128_cbc() : EVP_aes_256_cbc(); break;
    case SG_CIPHER_AES_CTR_NOPADDING:
        evp = (key_len == 16) ? EVP_aes_128_ctr() : EVP_aes_256_ctr(); break;
    default: return SG_ERR_UNKNOWN;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, evp, nullptr, key, iv);
    if (cipher == SG_CIPHER_AES_CTR_NOPADDING) EVP_CIPHER_CTX_set_padding(ctx, 0);

    std::vector<uint8_t> buf(data_len + EVP_CIPHER_block_size(evp));
    int len1 = 0, len2 = 0;
    EVP_DecryptUpdate(ctx, buf.data(), &len1, data, static_cast<int>(data_len));
    EVP_DecryptFinal_ex(ctx, buf.data() + len1, &len2);
    EVP_CIPHER_CTX_free(ctx);

    *out = signal_buffer_create(buf.data(), len1 + len2);
    return *out ? SG_SUCCESS : SG_ERR_NOMEM;
}

// ── CryptoEngine ──────────────────────────────────────────────────────────────

CryptoEngine::CryptoEngine() = default;

CryptoEngine::~CryptoEngine() {
    if (store_ctx_) {
        signal_protocol_store_context_destroy(store_ctx_);
        store_ctx_ = nullptr;
    }
    if (signal_ctx_) {
        signal_context_destroy(signal_ctx_);
        signal_ctx_ = nullptr;
    }
}

void CryptoEngine::setup_signal_crypto(signal_context* ctx) {
    signal_crypto_provider provider{};
    provider.random_func               = signal_random_bytes;
    provider.hmac_sha256_init_func     = signal_hmac_sha256_init;
    provider.hmac_sha256_update_func   = signal_hmac_sha256_update;
    provider.hmac_sha256_final_func    = signal_hmac_sha256_final;
    provider.hmac_sha256_cleanup_func  = signal_hmac_sha256_cleanup;
    provider.sha512_digest_init_func   = signal_sha512_digest_init;
    provider.sha512_digest_update_func = signal_sha512_digest_update;
    provider.sha512_digest_final_func  = signal_sha512_digest_final;
    provider.sha512_digest_cleanup_func= signal_sha512_digest_cleanup;
    provider.encrypt_func              = signal_encrypt;
    provider.decrypt_func              = signal_decrypt;
    signal_context_set_crypto_provider(ctx, &provider);
}

bool CryptoEngine::init(db::LocalStore& store, const ClientConfig& cfg,
                         const std::string& passphrase) {
    local_store_ = &store;
    const std::string& user_id = cfg.identity.user_id;

    // Init libsignal context
    if (signal_context_create(&signal_ctx_, nullptr) != SG_SUCCESS) {
        spdlog::error("Failed to create signal context");
        return false;
    }
    setup_signal_crypto(signal_ctx_);
    signal_context_set_log_function(signal_ctx_, nullptr);

    // Store context
    if (signal_protocol_store_context_create(&store_ctx_, signal_ctx_) != SG_SUCCESS) {
        spdlog::error("Failed to create signal store context");
        return false;
    }

    // Set up store adapters
    signal_store_ = std::make_unique<SignalStore>(store, user_id);
    signal_store_->register_with_context(store_ctx_);

    // Load or generate identity
    bool has_id = store.has_identity(user_id);
    if (has_id) {
        if (!identity_.load(user_id, passphrase, store)) {
            spdlog::error("Failed to load identity — wrong passphrase?");
            return false;
        }
    } else {
        if (!identity_.generate_and_save(user_id, passphrase, store)) {
            spdlog::error("Failed to generate identity");
            return false;
        }
    }

    // Generate SPK
    spk_        = identity_.generate_signed_prekey(1);
    next_opk_id_ = 1;

    // Group session
    group_session_ = std::make_unique<GroupSession>(store_ctx_, signal_ctx_);
    group_session_->set_local_identity(user_id);

    spdlog::info("CryptoEngine initialized for '{}'", user_id);
    return true;
}

KeyUpload CryptoEngine::prepare_key_upload(int num_opks) {
    KeyUpload ku;
    ku.set_signed_prekey(spk_.key_pair.pub.data(), spk_.key_pair.pub.size());
    ku.set_spk_signature(spk_.signature.data(), spk_.signature.size());
    ku.set_spk_id(spk_.id);

    auto opks = identity_.generate_one_time_prekeys(next_opk_id_, num_opks);
    for (auto& [id, kp] : opks) {
        ku.add_one_time_prekeys(kp.pub.data(), kp.pub.size());
        ku.add_opk_ids(id);

        // Store OPK private key in the pre_key store
        // Serialization: [id 4 bytes LE] [pub 32 bytes] [priv 32 bytes]
        std::vector<uint8_t> key_data(4 + 32 + 32);
        key_data[0] = (id >> 0) & 0xFF;
        key_data[1] = (id >> 8) & 0xFF;
        key_data[2] = (id >> 16) & 0xFF;
        key_data[3] = (id >> 24) & 0xFF;
        std::copy(kp.pub.begin(),  kp.pub.end(),  key_data.begin() + 4);
        std::copy(kp.priv.begin(), kp.priv.end(), key_data.begin() + 36);
        local_store_->store_pre_key(id, key_data);
    }
    next_opk_id_ += num_opks;
    return ku;
}

std::vector<uint8_t> CryptoEngine::sign_challenge(const std::vector<uint8_t>& nonce,
                                                    const std::string& user_id) {
    return identity_.sign_challenge(nonce, user_id);
}

std::vector<uint8_t> CryptoEngine::identity_pub() const {
    return {identity_.ed25519().pub.begin(), identity_.ed25519().pub.end()};
}

CryptoEngine::SpkInfo CryptoEngine::current_spk() const {
    return {
        {spk_.key_pair.pub.begin(), spk_.key_pair.pub.end()},
        spk_.signature
    };
}

// ── Encrypt ───────────────────────────────────────────────────────────────────

ChatEnvelope CryptoEngine::encrypt(const std::string& sender_id,
                                    const std::string& recipient_id,
                                    const std::string& plaintext,
                                    std::function<void(const std::string&)> key_request_fn) {
    ChatEnvelope env;
    env.set_sender_id(sender_id);
    env.set_recipient_id(recipient_id);

    // Check if we have a session for this recipient
    signal_protocol_address addr{};
    addr.name       = recipient_id.c_str();
    addr.name_len   = recipient_id.size();
    addr.device_id  = 1;

    bool has_session = signal_protocol_session_contains_session(store_ctx_, &addr) == 1;

    if (!has_session) {
        // Queue and request key bundle
        pending_plaintexts_[recipient_id] = plaintext;
        key_request_fn(recipient_id);
        // Return empty envelope (caller should not send yet)
        return {};
    }

    session_cipher* cipher = nullptr;
    int rc = session_cipher_create(&cipher, store_ctx_, &addr, signal_ctx_);
    if (rc != SG_SUCCESS) {
        spdlog::error("session_cipher_create failed: {}", rc);
        return {};
    }

    ciphertext_message* encrypted = nullptr;
    rc = session_cipher_encrypt(cipher,
        reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size(),
        &encrypted);
    session_cipher_free(cipher);

    if (rc != SG_SUCCESS) {
        spdlog::error("session_cipher_encrypt failed: {}", rc);
        return {};
    }

    signal_buffer* buf = ciphertext_message_get_serialized(encrypted);
    env.set_ciphertext(signal_buffer_data(buf), signal_buffer_len(buf));
    env.set_ciphertext_type(ciphertext_message_get_type(encrypted));
    SIGNAL_UNREF(encrypted);

    return env;
}

// ── Decrypt ───────────────────────────────────────────────────────────────────

DecryptResult CryptoEngine::decrypt(const ChatEnvelope& env) {
    DecryptResult result;
    result.sender_id = env.sender_id();

    signal_protocol_address addr{};
    addr.name      = env.sender_id().c_str();
    addr.name_len  = env.sender_id().size();
    addr.device_id = 1;

    session_cipher* cipher = nullptr;
    int rc = session_cipher_create(&cipher, store_ctx_, &addr, signal_ctx_);
    if (rc != SG_SUCCESS) {
        spdlog::warn("session_cipher_create failed for {}: {}", env.sender_id(), rc);
        return result;
    }

    const auto& ct_str = env.ciphertext();
    const uint8_t* ct  = reinterpret_cast<const uint8_t*>(ct_str.data());
    size_t ct_len       = ct_str.size();

    signal_buffer* plaintext_buf = nullptr;

    if (env.ciphertext_type() == 3) {  // PRE_KEY_SIGNAL_MESSAGE
        pre_key_signal_message* msg = nullptr;
        rc = pre_key_signal_message_deserialize(&msg, ct, ct_len, signal_ctx_);
        if (rc == SG_SUCCESS) {
            rc = session_cipher_decrypt_pre_key_signal_message(cipher, msg, nullptr, &plaintext_buf);
            SIGNAL_UNREF(msg);
        }
    } else {  // SIGNAL_MESSAGE
        signal_message* msg = nullptr;
        rc = signal_message_deserialize(&msg, ct, ct_len, signal_ctx_);
        if (rc == SG_SUCCESS) {
            rc = session_cipher_decrypt_signal_message(cipher, msg, nullptr, &plaintext_buf);
            SIGNAL_UNREF(msg);
        }
    }

    session_cipher_free(cipher);

    if (rc != SG_SUCCESS || !plaintext_buf) {
        spdlog::warn("Decryption failed for message from {}: {}", env.sender_id(), rc);
        return result;
    }

    result.plaintext.assign(
        reinterpret_cast<const char*>(signal_buffer_data(plaintext_buf)),
        signal_buffer_len(plaintext_buf));
    signal_buffer_free(plaintext_buf);
    result.success = true;
    return result;
}

// ── Key bundle handling ───────────────────────────────────────────────────────

void CryptoEngine::on_key_bundle(const KeyBundle& bundle, const std::string& recipient_id) {
    // Build session_pre_key_bundle from the KeyBundle proto
    ec_public_key* identity_key   = nullptr;
    ec_public_key* signed_pre_key = nullptr;
    ec_public_key* one_time_key   = nullptr;

    const auto& id_pub_str = bundle.identity_pub();
    const auto& spk_str    = bundle.signed_prekey();
    const auto& opk_str    = bundle.one_time_prekey();

    curve_decode_point(&identity_key,
        reinterpret_cast<const uint8_t*>(id_pub_str.data()), id_pub_str.size(), signal_ctx_);
    curve_decode_point(&signed_pre_key,
        reinterpret_cast<const uint8_t*>(spk_str.data()), spk_str.size(), signal_ctx_);
    if (!opk_str.empty()) {
        curve_decode_point(&one_time_key,
            reinterpret_cast<const uint8_t*>(opk_str.data()), opk_str.size(), signal_ctx_);
    }

    const auto& spk_sig = bundle.spk_signature();

    session_pre_key_bundle* pkb = nullptr;
    int rc = session_pre_key_bundle_create(&pkb,
        1,  // registration_id
        1,  // device_id
        bundle.opk_id(), one_time_key,
        bundle.spk_id(), signed_pre_key,
        reinterpret_cast<const uint8_t*>(spk_sig.data()), spk_sig.size(),
        identity_key);

    if (rc != SG_SUCCESS) {
        spdlog::error("session_pre_key_bundle_create failed: {}", rc);
        SIGNAL_UNREF(identity_key);
        SIGNAL_UNREF(signed_pre_key);
        if (one_time_key) SIGNAL_UNREF(one_time_key);
        return;
    }

    signal_protocol_address addr{};
    addr.name      = recipient_id.c_str();
    addr.name_len  = recipient_id.size();
    addr.device_id = 1;

    session_builder* builder = nullptr;
    rc = session_builder_create(&builder, store_ctx_, &addr, signal_ctx_);
    if (rc == SG_SUCCESS) {
        rc = session_builder_process_pre_key_bundle(builder, pkb);
        session_builder_free(builder);
    }

    SIGNAL_UNREF(pkb);
    SIGNAL_UNREF(identity_key);
    SIGNAL_UNREF(signed_pre_key);
    if (one_time_key) SIGNAL_UNREF(one_time_key);

    if (rc != SG_SUCCESS) {
        spdlog::error("session_builder_process_pre_key_bundle failed for {}: {}", recipient_id, rc);
        return;
    }

    spdlog::info("Established X3DH session with '{}'", recipient_id);
}

std::string CryptoEngine::safety_number(const std::string& peer_id, db::LocalStore& store) {
    auto peer_pub = store.load_peer_identity(peer_id);
    if (!peer_pub || peer_pub->size() != 32) {
        return "(no key on file for " + peer_id + ")";
    }
    std::array<uint8_t, 32> their_pub{};
    std::copy(peer_pub->begin(), peer_pub->end(), their_pub.begin());
    return Identity::safety_number(identity_.ed25519().pub, their_pub);
}

} // namespace ircord::crypto
