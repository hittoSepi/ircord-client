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
};

struct VoiceConfig {
    std::string input_device;
    std::string output_device;
    int         opus_bitrate = 64000;
    int         frame_ms     = 20;
};

struct PreviewConfig {
    bool enabled       = true;
    int  fetch_timeout = 5;
    int  max_cache     = 200;
};

struct TlsConfig {
    bool verify_peer = true;
};

struct ClientConfig {
    ServerConfig   server;
    IdentityConfig identity;
    UiConfig       ui;
    VoiceConfig    voice;
    PreviewConfig  preview;
    TlsConfig      tls;

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

} // namespace ircord
