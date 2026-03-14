#pragma once
#include <string>
#include <cstdint>
#include <filesystem>

namespace ircord {

struct ServerConfig {
    std::string host = "localhost";
    uint16_t    port = 6667;
    std::string cert_pin;   // SHA-256 hex fingerprint for cert pinning
};

struct IdentityConfig {
    std::string user_id  = "user";
    std::string key_file;   // empty = default platform path
};

struct UiConfig {
    std::string theme             = "tokyo-night";
    std::string timestamp_format  = "%H:%M";
    int         max_messages      = 1000;
    int         font_scale        = 100;     // Percentage (100 = 100%)
    bool        show_timestamps   = true;
    bool        show_user_colors  = true;
    // User list panel settings (persisted)
    int         user_list_width   = 20;      // Width in columns
    bool        user_list_collapsed = false; // Start collapsed?
};

struct VoiceConfig {
    std::string input_device;
    std::string output_device;
    int         opus_bitrate  = 64000;
    int         frame_ms      = 20;
    std::string mode          = "ptt";     // "ptt" or "vox"
    std::string ptt_key       = "F1";
    float       vad_threshold = 0.02f;
};

struct PreviewConfig {
    bool enabled       = true;
    int  fetch_timeout = 5;
    int  max_cache     = 200;
};

struct TlsConfig {
    bool verify_peer = true;
};

struct ConnectionConfig {
    bool auto_reconnect      = true;
    int  reconnect_delay_sec = 5;
    int  timeout_sec         = 30;
};

struct NotificationConfig {
    bool        desktop_notifications = true;
    bool        sound_alerts          = true;
    bool        notify_on_mention     = true;
    bool        notify_on_dm          = true;
    std::string mention_keywords;          // Comma-separated list
};

struct ClientConfig {
    ServerConfig       server;
    IdentityConfig     identity;
    UiConfig           ui;
    VoiceConfig        voice;
    PreviewConfig      preview;
    TlsConfig          tls;
    ConnectionConfig   connection;
    NotificationConfig notifications;

    // Derived: platform-specific config directory
    std::filesystem::path config_dir;
    std::filesystem::path db_path;
};

// Load config from TOML file. Returns default config on failure.
ClientConfig load_config(const std::filesystem::path& path);

// Save config back to TOML file (creates file if missing).
void save_config(const ClientConfig& cfg, const std::filesystem::path& path);

// Returns the default platform config directory:
//   Windows: %APPDATA%\ircord\
//   Linux:   ~/.config/ircord/
std::filesystem::path default_config_dir();

// Returns the default path for remembered login credentials.
std::filesystem::path default_credentials_path();

// Clears remembered credentials and local encrypted client state.
bool clear_local_client_state(ClientConfig& cfg,
                              const std::filesystem::path& config_path,
                              std::string* status_message = nullptr);

// Export settings to a backup file
void export_settings(const ClientConfig& cfg, const std::filesystem::path& path);

// Import settings from a backup file (returns true on success)
bool import_settings(ClientConfig& cfg, const std::filesystem::path& path);

} // namespace ircord
