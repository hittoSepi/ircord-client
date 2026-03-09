#pragma once
#include <SQLiteCpp/SQLiteCpp.h>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ircord::db {

// Thread-safe SQLite wrapper for all client-local persistent state.
class LocalStore {
public:
    explicit LocalStore(const std::filesystem::path& db_path);
    ~LocalStore() = default;

    // ── Identity ─────────────────────────────────────────────────────────
    struct IdentityRow {
        std::string       user_id;
        std::vector<uint8_t> identity_pub;
        std::vector<uint8_t> identity_priv_enc;  // encrypted
        std::vector<uint8_t> salt;
        int64_t           created_at;
    };
    bool             has_identity(const std::string& user_id);
    void             save_identity(const IdentityRow& row);
    std::optional<IdentityRow> load_identity(const std::string& user_id);

    // ── Signal: Sessions ─────────────────────────────────────────────────
    std::optional<std::vector<uint8_t>> load_session(const std::string& name, int device_id);
    void save_session(const std::string& name, int device_id,
                      const std::vector<uint8_t>& session_data);
    void delete_session(const std::string& name, int device_id);
    std::vector<int> get_sub_device_sessions(const std::string& name);
    bool contains_session(const std::string& name, int device_id);

    // ── Signal: Pre-keys ─────────────────────────────────────────────────
    std::optional<std::vector<uint8_t>> load_pre_key(uint32_t id);
    void store_pre_key(uint32_t id, const std::vector<uint8_t>& key_data);
    void remove_pre_key(uint32_t id);
    bool contains_pre_key(uint32_t id);

    // ── Signal: Signed pre-keys ───────────────────────────────────────────
    std::optional<std::vector<uint8_t>> load_signed_pre_key(uint32_t id);
    void store_signed_pre_key(uint32_t id, const std::vector<uint8_t>& key_data);
    void remove_signed_pre_key(uint32_t id);
    bool contains_signed_pre_key(uint32_t id);

    // ── Signal: Peer identities ───────────────────────────────────────────
    std::optional<std::vector<uint8_t>> load_peer_identity(const std::string& name);
    void save_peer_identity(const std::string& name,
                            const std::vector<uint8_t>& identity_pub,
                            const std::string& trust_status = "unverified");
    std::string get_trust_status(const std::string& name);

    // ── Signal: Sender keys ───────────────────────────────────────────────
    std::optional<std::vector<uint8_t>> load_sender_key(
        const std::string& group_id, const std::string& sender_id, int device_id);
    void store_sender_key(const std::string& group_id, const std::string& sender_id,
                          int device_id, const std::vector<uint8_t>& key_data);

    // ── Server certificate pin ────────────────────────────────────────────
    std::optional<std::string> load_cert_pin(const std::string& host);
    void save_cert_pin(const std::string& host, const std::string& fingerprint);

    // ── UI state ─────────────────────────────────────────────────────────
    std::optional<std::string> get_ui_state(const std::string& key);
    void set_ui_state(const std::string& key, const std::string& value);

    // ── Link preview cache ────────────────────────────────────────────────
    struct PreviewRow { std::string title, description; int64_t fetched_at; };
    std::optional<PreviewRow> get_cached_preview(const std::string& url);
    void cache_preview(const std::string& url, const std::string& title,
                       const std::string& description);

    // Direct DB access for migrations
    SQLite::Database& db() { return db_; }

private:
    std::mutex       mu_;
    SQLite::Database db_;

    static std::vector<uint8_t> blob_to_vec(const SQLite::Column& col);
};

} // namespace ircord::db
