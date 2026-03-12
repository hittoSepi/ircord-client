#include "db/migrations.hpp"
#include <spdlog/spdlog.h>

namespace ircord::db {

static constexpr int kCurrentVersion = 4;

// Migration 1: core message & identity tables
static void migration_1(SQLite::Database& db) {
    db.exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            channel_id  TEXT    NOT NULL,
            sender_id   TEXT    NOT NULL,
            content     TEXT    NOT NULL,
            timestamp_ms INTEGER NOT NULL,
            type        INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_messages_channel ON messages(channel_id, timestamp_ms);

        CREATE TABLE IF NOT EXISTS identity (
            user_id         TEXT PRIMARY KEY,
            identity_pub    BLOB NOT NULL,
            identity_priv   BLOB NOT NULL,   -- encrypted at rest
            salt            BLOB NOT NULL,
            created_at      INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS ui_state (
            key   TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )");
}

// Migration 2: Signal Protocol session/key tables
static void migration_2(SQLite::Database& db) {
    db.exec(R"(
        CREATE TABLE IF NOT EXISTS signal_sessions (
            name        TEXT    NOT NULL,
            device_id   INTEGER NOT NULL,
            session     BLOB    NOT NULL,
            PRIMARY KEY (name, device_id)
        );

        CREATE TABLE IF NOT EXISTS pre_keys (
            id      INTEGER PRIMARY KEY,
            key     BLOB    NOT NULL
        );

        CREATE TABLE IF NOT EXISTS signed_pre_keys (
            id      INTEGER PRIMARY KEY,
            key     BLOB    NOT NULL
        );

        CREATE TABLE IF NOT EXISTS peer_identities (
            name         TEXT PRIMARY KEY,
            identity_pub BLOB NOT NULL,
            trust_status TEXT NOT NULL DEFAULT 'unverified'
        );

        CREATE TABLE IF NOT EXISTS sender_keys (
            group_id    TEXT    NOT NULL,
            sender_id   TEXT    NOT NULL,
            device_id   INTEGER NOT NULL,
            key_record  BLOB    NOT NULL,
            PRIMARY KEY (group_id, sender_id, device_id)
        );
    )");
}

// Migration 3: server certificate pinning & misc
static void migration_3(SQLite::Database& db) {
    db.exec(R"(
        CREATE TABLE IF NOT EXISTS server_pins (
            host        TEXT PRIMARY KEY,
            fingerprint TEXT NOT NULL,
            pinned_at   INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS link_preview_cache (
            url         TEXT PRIMARY KEY,
            title       TEXT,
            description TEXT,
            fetched_at  INTEGER NOT NULL
        );
    )");
}

// Migration 4: Full-text search for messages using FTS5 (optional)
static void migration_4(SQLite::Database& db) {
    // Check if FTS5 is available
    bool fts5_available = false;
    try {
        // Try to create a test FTS5 table to verify support
        db.exec("CREATE VIRTUAL TABLE IF NOT EXISTS _fts5_test USING fts5(test)");
        db.exec("DROP TABLE IF EXISTS _fts5_test");
        fts5_available = true;
        spdlog::info("FTS5 support detected");
    } catch (const std::exception& e) {
        spdlog::warn("FTS5 not available: {}", e.what());
    }

    if (fts5_available) {
        db.exec(R"(
            -- FTS5 virtual table for message content search
            CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5(
                content,
                content_rowid=rowid,
                content='messages'
            );

            -- Trigger to keep FTS index updated on insert
            CREATE TRIGGER IF NOT EXISTS messages_ai AFTER INSERT ON messages BEGIN
                INSERT INTO messages_fts(rowid, content) VALUES (new.rowid, new.content);
            END;

            -- Trigger to keep FTS index updated on delete
            CREATE TRIGGER IF NOT EXISTS messages_ad AFTER DELETE ON messages BEGIN
                INSERT INTO messages_fts(messages_fts, rowid, content) 
                VALUES ('delete', old.rowid, old.content);
            END;
        )");
    } else {
        spdlog::info("Skipping FTS5 virtual table creation");
    }

    // Index for faster sender-based queries
    db.exec("CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender_id, timestamp_ms);");
}

void apply_migrations(SQLite::Database& db) {
    // Enable WAL mode for performance
    db.exec("PRAGMA journal_mode=WAL;");
    db.exec("PRAGMA foreign_keys=ON;");

    int version = db.execAndGet("PRAGMA user_version;").getInt();
    spdlog::debug("DB schema version: {}", version);

    SQLite::Transaction txn(db);

    if (version < 1) {
        spdlog::info("Applying DB migration 1");
        migration_1(db);
    }
    if (version < 2) {
        spdlog::info("Applying DB migration 2");
        migration_2(db);
    }
    if (version < 3) {
        spdlog::info("Applying DB migration 3");
        migration_3(db);
    }
    if (version < 4) {
        spdlog::info("Applying DB migration 4");
        migration_4(db);
    }

    if (version < kCurrentVersion) {
        db.exec("PRAGMA user_version = " + std::to_string(kCurrentVersion) + ";");
    }

    txn.commit();
}

} // namespace ircord::db
