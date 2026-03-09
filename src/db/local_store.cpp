#include "db/local_store.hpp"
#include "db/migrations.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <stdexcept>

namespace ircord::db {

LocalStore::LocalStore(const std::filesystem::path& db_path)
    : db_(db_path.string(),
          SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    apply_migrations(db_);
}

std::vector<uint8_t> LocalStore::blob_to_vec(const SQLite::Column& col) {
    const void* ptr  = col.getBlob();
    int         size = col.getBytes();
    if (!ptr || size <= 0) return {};
    const uint8_t* p = static_cast<const uint8_t*>(ptr);
    return std::vector<uint8_t>(p, p + size);
}

// ── Identity ──────────────────────────────────────────────────────────────────

bool LocalStore::has_identity(const std::string& user_id) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "SELECT COUNT(*) FROM identity WHERE user_id = ?");
    q.bind(1, user_id);
    return q.executeStep() && q.getColumn(0).getInt() > 0;
}

void LocalStore::save_identity(const IdentityRow& row) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "INSERT OR REPLACE INTO identity "
        "(user_id, identity_pub, identity_priv, salt, created_at) "
        "VALUES (?, ?, ?, ?, ?)");
    q.bind(1, row.user_id);
    q.bind(2, row.identity_pub.data(), static_cast<int>(row.identity_pub.size()));
    q.bind(3, row.identity_priv_enc.data(), static_cast<int>(row.identity_priv_enc.size()));
    q.bind(4, row.salt.data(), static_cast<int>(row.salt.size()));
    q.bind(5, static_cast<int64_t>(row.created_at));
    q.exec();
}

std::optional<LocalStore::IdentityRow> LocalStore::load_identity(const std::string& user_id) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "SELECT user_id, identity_pub, identity_priv, salt, created_at "
        "FROM identity WHERE user_id = ?");
    q.bind(1, user_id);
    if (!q.executeStep()) return std::nullopt;
    IdentityRow row;
    row.user_id          = q.getColumn(0).getString();
    row.identity_pub     = blob_to_vec(q.getColumn(1));
    row.identity_priv_enc= blob_to_vec(q.getColumn(2));
    row.salt             = blob_to_vec(q.getColumn(3));
    row.created_at       = q.getColumn(4).getInt64();
    return row;
}

// ── Sessions ──────────────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> LocalStore::load_session(
    const std::string& name, int device_id) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "SELECT session FROM signal_sessions WHERE name=? AND device_id=?");
    q.bind(1, name); q.bind(2, device_id);
    if (!q.executeStep()) return std::nullopt;
    return blob_to_vec(q.getColumn(0));
}

void LocalStore::save_session(const std::string& name, int device_id,
                               const std::vector<uint8_t>& data) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "INSERT OR REPLACE INTO signal_sessions (name, device_id, session) VALUES (?,?,?)");
    q.bind(1, name); q.bind(2, device_id);
    q.bind(3, data.data(), static_cast<int>(data.size()));
    q.exec();
}

void LocalStore::delete_session(const std::string& name, int device_id) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "DELETE FROM signal_sessions WHERE name=? AND device_id=?");
    q.bind(1, name); q.bind(2, device_id);
    q.exec();
}

std::vector<int> LocalStore::get_sub_device_sessions(const std::string& name) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "SELECT device_id FROM signal_sessions WHERE name=?");
    q.bind(1, name);
    std::vector<int> ids;
    while (q.executeStep()) ids.push_back(q.getColumn(0).getInt());
    return ids;
}

bool LocalStore::contains_session(const std::string& name, int device_id) {
    return load_session(name, device_id).has_value();
}

// ── Pre-keys ──────────────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> LocalStore::load_pre_key(uint32_t id) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_, "SELECT key FROM pre_keys WHERE id=?");
    q.bind(1, static_cast<int64_t>(id));
    if (!q.executeStep()) return std::nullopt;
    return blob_to_vec(q.getColumn(0));
}

void LocalStore::store_pre_key(uint32_t id, const std::vector<uint8_t>& data) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_, "INSERT OR REPLACE INTO pre_keys (id, key) VALUES (?,?)");
    q.bind(1, static_cast<int64_t>(id));
    q.bind(2, data.data(), static_cast<int>(data.size()));
    q.exec();
}

void LocalStore::remove_pre_key(uint32_t id) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_, "DELETE FROM pre_keys WHERE id=?");
    q.bind(1, static_cast<int64_t>(id));
    q.exec();
}

bool LocalStore::contains_pre_key(uint32_t id) {
    return load_pre_key(id).has_value();
}

// ── Signed pre-keys ───────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> LocalStore::load_signed_pre_key(uint32_t id) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_, "SELECT key FROM signed_pre_keys WHERE id=?");
    q.bind(1, static_cast<int64_t>(id));
    if (!q.executeStep()) return std::nullopt;
    return blob_to_vec(q.getColumn(0));
}

void LocalStore::store_signed_pre_key(uint32_t id, const std::vector<uint8_t>& data) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_, "INSERT OR REPLACE INTO signed_pre_keys (id, key) VALUES (?,?)");
    q.bind(1, static_cast<int64_t>(id));
    q.bind(2, data.data(), static_cast<int>(data.size()));
    q.exec();
}

void LocalStore::remove_signed_pre_key(uint32_t id) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_, "DELETE FROM signed_pre_keys WHERE id=?");
    q.bind(1, static_cast<int64_t>(id));
    q.exec();
}

bool LocalStore::contains_signed_pre_key(uint32_t id) {
    return load_signed_pre_key(id).has_value();
}

// ── Peer identities ───────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> LocalStore::load_peer_identity(const std::string& name) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_, "SELECT identity_pub FROM peer_identities WHERE name=?");
    q.bind(1, name);
    if (!q.executeStep()) return std::nullopt;
    return blob_to_vec(q.getColumn(0));
}

void LocalStore::save_peer_identity(const std::string& name,
                                     const std::vector<uint8_t>& pub,
                                     const std::string& trust_status) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "INSERT OR REPLACE INTO peer_identities (name, identity_pub, trust_status) "
        "VALUES (?,?,?)");
    q.bind(1, name);
    q.bind(2, pub.data(), static_cast<int>(pub.size()));
    q.bind(3, trust_status);
    q.exec();
}

std::string LocalStore::get_trust_status(const std::string& name) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_, "SELECT trust_status FROM peer_identities WHERE name=?");
    q.bind(1, name);
    if (!q.executeStep()) return "unknown";
    return q.getColumn(0).getString();
}

// ── Sender keys ───────────────────────────────────────────────────────────────

std::optional<std::vector<uint8_t>> LocalStore::load_sender_key(
    const std::string& group_id, const std::string& sender_id, int device_id) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "SELECT key_record FROM sender_keys WHERE group_id=? AND sender_id=? AND device_id=?");
    q.bind(1, group_id); q.bind(2, sender_id); q.bind(3, device_id);
    if (!q.executeStep()) return std::nullopt;
    return blob_to_vec(q.getColumn(0));
}

void LocalStore::store_sender_key(const std::string& group_id, const std::string& sender_id,
                                    int device_id, const std::vector<uint8_t>& data) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "INSERT OR REPLACE INTO sender_keys (group_id, sender_id, device_id, key_record) "
        "VALUES (?,?,?,?)");
    q.bind(1, group_id); q.bind(2, sender_id); q.bind(3, device_id);
    q.bind(4, data.data(), static_cast<int>(data.size()));
    q.exec();
}

// ── Cert pin ──────────────────────────────────────────────────────────────────

std::optional<std::string> LocalStore::load_cert_pin(const std::string& host) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_, "SELECT fingerprint FROM server_pins WHERE host=?");
    q.bind(1, host);
    if (!q.executeStep()) return std::nullopt;
    return q.getColumn(0).getString();
}

void LocalStore::save_cert_pin(const std::string& host, const std::string& fp) {
    std::lock_guard lk(mu_);
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    SQLite::Statement q(db_,
        "INSERT OR REPLACE INTO server_pins (host, fingerprint, pinned_at) VALUES (?,?,?)");
    q.bind(1, host); q.bind(2, fp); q.bind(3, now);
    q.exec();
}

// ── UI state ─────────────────────────────────────────────────────────────────

std::optional<std::string> LocalStore::get_ui_state(const std::string& key) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_, "SELECT value FROM ui_state WHERE key=?");
    q.bind(1, key);
    if (!q.executeStep()) return std::nullopt;
    return q.getColumn(0).getString();
}

void LocalStore::set_ui_state(const std::string& key, const std::string& value) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "INSERT OR REPLACE INTO ui_state (key, value) VALUES (?,?)");
    q.bind(1, key); q.bind(2, value);
    q.exec();
}

// ── Link preview cache ────────────────────────────────────────────────────────

std::optional<LocalStore::PreviewRow> LocalStore::get_cached_preview(const std::string& url) {
    std::lock_guard lk(mu_);
    SQLite::Statement q(db_,
        "SELECT title, description, fetched_at FROM link_preview_cache WHERE url=?");
    q.bind(1, url);
    if (!q.executeStep()) return std::nullopt;
    PreviewRow row;
    row.title       = q.getColumn(0).getString();
    row.description = q.getColumn(1).getString();
    row.fetched_at  = q.getColumn(2).getInt64();
    return row;
}

void LocalStore::cache_preview(const std::string& url, const std::string& title,
                                const std::string& desc) {
    std::lock_guard lk(mu_);
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    SQLite::Statement q(db_,
        "INSERT OR REPLACE INTO link_preview_cache (url, title, description, fetched_at) "
        "VALUES (?,?,?,?)");
    q.bind(1, url); q.bind(2, title); q.bind(3, desc); q.bind(4, now);
    q.exec();
}

} // namespace ircord::db
