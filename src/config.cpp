#include "config.hpp"
#include <toml.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <fstream>

namespace ircord {

std::filesystem::path default_config_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        return std::filesystem::path(appdata) / "ircord";
    }
    return std::filesystem::path(".");
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".config" / "ircord";
    }
    return std::filesystem::path(".");
#endif
}

ClientConfig load_config(const std::filesystem::path& path) {
    ClientConfig cfg;
    cfg.config_dir = default_config_dir();
    cfg.db_path    = cfg.config_dir / "ircord.db";

    if (path.empty() || !std::filesystem::exists(path)) {
        spdlog::warn("Config file not found at '{}', using defaults", path.string());
        return cfg;
    }

    try {
        auto data = toml::parse(path.string());

        if (data.contains("server")) {
            auto& s = data.at("server");
            if (s.contains("host")) cfg.server.host = toml::find<std::string>(s, "host");
            if (s.contains("port")) cfg.server.port = static_cast<uint16_t>(toml::find<int>(s, "port"));
            if (s.contains("cert_pin")) cfg.server.cert_pin = toml::find<std::string>(s, "cert_pin");
        }

        if (data.contains("identity")) {
            auto& id = data.at("identity");
            if (id.contains("user_id"))  cfg.identity.user_id  = toml::find<std::string>(id, "user_id");
            if (id.contains("key_file")) cfg.identity.key_file = toml::find<std::string>(id, "key_file");
        }

        if (data.contains("ui")) {
            auto& ui = data.at("ui");
            if (ui.contains("theme"))            cfg.ui.theme            = toml::find<std::string>(ui, "theme");
            if (ui.contains("timestamp_format")) cfg.ui.timestamp_format = toml::find<std::string>(ui, "timestamp_format");
            if (ui.contains("max_messages"))     cfg.ui.max_messages     = toml::find<int>(ui, "max_messages");
        }

        if (data.contains("voice")) {
            auto& v = data.at("voice");
            if (v.contains("input_device"))  cfg.voice.input_device  = toml::find<std::string>(v, "input_device");
            if (v.contains("output_device")) cfg.voice.output_device = toml::find<std::string>(v, "output_device");
            if (v.contains("opus_bitrate"))  cfg.voice.opus_bitrate  = toml::find<int>(v, "opus_bitrate");
            if (v.contains("frame_ms"))      cfg.voice.frame_ms      = toml::find<int>(v, "frame_ms");
        }

        if (data.contains("preview")) {
            auto& p = data.at("preview");
            if (p.contains("enabled"))       cfg.preview.enabled       = toml::find<bool>(p, "enabled");
            if (p.contains("fetch_timeout")) cfg.preview.fetch_timeout = toml::find<int>(p, "fetch_timeout");
            if (p.contains("max_cache"))     cfg.preview.max_cache     = toml::find<int>(p, "max_cache");
        }

        if (data.contains("tls")) {
            auto& t = data.at("tls");
            if (t.contains("verify_peer")) cfg.tls.verify_peer = toml::find<bool>(t, "verify_peer");
        }

    } catch (const std::exception& e) {
        spdlog::error("Failed to parse config '{}': {}", path.string(), e.what());
    }

    return cfg;
}

void save_config(const ClientConfig& cfg, const std::filesystem::path& path) {
    // Load existing file if present, so we preserve unknown keys
    toml::value data;
    if (!path.empty() && std::filesystem::exists(path)) {
        try {
            data = toml::parse(path.string());
        } catch (...) {
            data = toml::value{};
        }
    }

    // Patch identity section
    if (!data.contains("identity")) {
        data["identity"] = toml::value{};
    }
    data["identity"]["user_id"] = cfg.identity.user_id;
    if (!cfg.identity.key_file.empty())
        data["identity"]["key_file"] = cfg.identity.key_file;

    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream ofs(path);
        ofs << data;
    } catch (const std::exception& e) {
        spdlog::error("Failed to save config '{}': {}", path.string(), e.what());
    }
}

} // namespace ircord
