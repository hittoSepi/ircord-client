#include "config.hpp"
#include <toml.hpp>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <vector>

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

std::filesystem::path default_credentials_path() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        return std::filesystem::path(appdata) / "ircord" / "credentials.enc";
    }
#else
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".config" / "ircord" / "credentials.enc";
    }
#endif
    return std::filesystem::path("credentials.enc");
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
            if (id.contains("password")) cfg.identity.password = toml::find<std::string>(id, "password");
        }

        if (data.contains("ui")) {
            auto& ui = data.at("ui");
            if (ui.contains("theme"))            cfg.ui.theme            = toml::find<std::string>(ui, "theme");
            if (ui.contains("timestamp_format")) cfg.ui.timestamp_format = toml::find<std::string>(ui, "timestamp_format");
            if (ui.contains("max_messages"))     cfg.ui.max_messages     = toml::find<int>(ui, "max_messages");
            if (ui.contains("font_scale"))       cfg.ui.font_scale       = toml::find<int>(ui, "font_scale");
            if (ui.contains("show_timestamps"))  cfg.ui.show_timestamps  = toml::find<bool>(ui, "show_timestamps");
            if (ui.contains("show_user_colors")) cfg.ui.show_user_colors = toml::find<bool>(ui, "show_user_colors");
            if (ui.contains("user_list_width"))  cfg.ui.user_list_width  = toml::find<int>(ui, "user_list_width");
            if (ui.contains("user_list_collapsed")) {
                cfg.ui.user_list_collapsed = toml::find<bool>(ui, "user_list_collapsed");
            }
        }

        if (data.contains("voice")) {
            auto& v = data.at("voice");
            if (v.contains("input_device"))  cfg.voice.input_device  = toml::find<std::string>(v, "input_device");
            if (v.contains("output_device")) cfg.voice.output_device = toml::find<std::string>(v, "output_device");
            if (v.contains("opus_bitrate"))  cfg.voice.opus_bitrate  = toml::find<int>(v, "opus_bitrate");
            if (v.contains("frame_ms"))      cfg.voice.frame_ms      = toml::find<int>(v, "frame_ms");
            if (v.contains("mode"))          cfg.voice.mode          = toml::find<std::string>(v, "mode");
            if (v.contains("ptt_key"))       cfg.voice.ptt_key       = toml::find<std::string>(v, "ptt_key");
            if (v.contains("vad_threshold")) cfg.voice.vad_threshold = static_cast<float>(toml::find<double>(v, "vad_threshold"));
            if (v.contains("ice_servers"))   cfg.voice.ice_servers   = toml::find<std::vector<std::string>>(v, "ice_servers");
            if (v.contains("turn_username")) cfg.voice.turn_username = toml::find<std::string>(v, "turn_username");
            if (v.contains("turn_password")) cfg.voice.turn_password = toml::find<std::string>(v, "turn_password");
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

        if (data.contains("connection")) {
            auto& c = data.at("connection");
            if (c.contains("auto_reconnect"))      cfg.connection.auto_reconnect      = toml::find<bool>(c, "auto_reconnect");
            if (c.contains("reconnect_delay_sec")) cfg.connection.reconnect_delay_sec = toml::find<int>(c, "reconnect_delay_sec");
            if (c.contains("timeout_sec"))         cfg.connection.timeout_sec         = toml::find<int>(c, "timeout_sec");
        }

        if (data.contains("notifications")) {
            auto& n = data.at("notifications");
            if (n.contains("desktop_notifications")) cfg.notifications.desktop_notifications = toml::find<bool>(n, "desktop_notifications");
            if (n.contains("sound_alerts"))          cfg.notifications.sound_alerts          = toml::find<bool>(n, "sound_alerts");
            if (n.contains("notify_on_mention"))     cfg.notifications.notify_on_mention     = toml::find<bool>(n, "notify_on_mention");
            if (n.contains("notify_on_dm"))          cfg.notifications.notify_on_dm          = toml::find<bool>(n, "notify_on_dm");
            if (n.contains("mention_keywords"))      cfg.notifications.mention_keywords      = toml::find<std::string>(n, "mention_keywords");
        }

    } catch (const std::exception& e) {
        spdlog::error("Failed to parse config '{}': {}", path.string(), e.what());
    }

    return cfg;
}

bool clear_local_client_state(ClientConfig& cfg,
                              const std::filesystem::path& config_path,
                              std::string* status_message) {
    std::vector<std::filesystem::path> targets;
    auto add_target = [&targets](const std::filesystem::path& path) {
        if (path.empty()) {
            return;
        }
        if (std::find(targets.begin(), targets.end(), path) == targets.end()) {
            targets.push_back(path);
        }
    };

    add_target(default_credentials_path());
    add_target(cfg.db_path);
    if (!cfg.identity.key_file.empty()) {
        add_target(cfg.identity.key_file);
    }
    add_target(cfg.config_dir / "identity.key");

    size_t removed_count = 0;
    std::string failed_path;
    std::string failed_reason;

    for (const auto& target : targets) {
        try {
            if (!std::filesystem::exists(target)) {
                continue;
            }

            if (std::filesystem::is_directory(target)) {
                removed_count += std::filesystem::remove_all(target);
            } else if (std::filesystem::remove(target)) {
                ++removed_count;
            }
        } catch (const std::exception& e) {
            failed_path = target.string();
            failed_reason = e.what();
            break;
        }
    }

    if (!failed_reason.empty()) {
        if (status_message) {
            *status_message = "Failed to clear local data at " + failed_path + ": " + failed_reason;
        }
        spdlog::error("Failed to clear local client state at '{}': {}", failed_path, failed_reason);
        return false;
    }

    cfg.identity.user_id = "user";
    cfg.identity.key_file.clear();
    cfg.server.cert_pin.clear();
    save_config(cfg, config_path);

    if (status_message) {
        if (removed_count > 0) {
            *status_message = "Local credentials and identity data cleared. Enter the same username and passkey to re-sync keys.";
        } else {
            *status_message = "No local credential files were found. Enter the same username and passkey to re-sync keys.";
        }
    }

    spdlog::info("Cleared local client state ({} entries removed)", removed_count);
    return true;
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

    // Patch server section
    data["server"]["host"] = cfg.server.host;
    data["server"]["port"] = cfg.server.port;
    if (!cfg.server.cert_pin.empty()) {
        data["server"]["cert_pin"] = cfg.server.cert_pin;
    }

    // Patch identity section
    data["identity"]["user_id"] = cfg.identity.user_id;
    if (!cfg.identity.key_file.empty()) {
        data["identity"]["key_file"] = cfg.identity.key_file;
    }

    // Patch UI section
    data["ui"]["theme"] = cfg.ui.theme;
    data["ui"]["timestamp_format"] = cfg.ui.timestamp_format;
    data["ui"]["max_messages"] = cfg.ui.max_messages;
    data["ui"]["font_scale"] = cfg.ui.font_scale;
    data["ui"]["show_timestamps"] = cfg.ui.show_timestamps;
    data["ui"]["show_user_colors"] = cfg.ui.show_user_colors;
    data["ui"]["user_list_width"] = cfg.ui.user_list_width;
    data["ui"]["user_list_collapsed"] = cfg.ui.user_list_collapsed;

    // Patch voice section
    data["voice"]["input_device"] = cfg.voice.input_device;
    data["voice"]["output_device"] = cfg.voice.output_device;
    data["voice"]["opus_bitrate"] = cfg.voice.opus_bitrate;
    data["voice"]["frame_ms"] = cfg.voice.frame_ms;
    data["voice"]["mode"] = cfg.voice.mode;
    data["voice"]["ptt_key"] = cfg.voice.ptt_key;
    data["voice"]["vad_threshold"] = cfg.voice.vad_threshold;
    toml::array voice_ice_servers;
    for (const auto& server : cfg.voice.ice_servers) {
        voice_ice_servers.push_back(server);
    }
    data["voice"]["ice_servers"] = voice_ice_servers;
    data["voice"]["turn_username"] = cfg.voice.turn_username;
    data["voice"]["turn_password"] = cfg.voice.turn_password;

    // Patch preview section
    data["preview"]["enabled"] = cfg.preview.enabled;
    data["preview"]["fetch_timeout"] = cfg.preview.fetch_timeout;
    data["preview"]["max_cache"] = cfg.preview.max_cache;

    // Patch TLS section
    data["tls"]["verify_peer"] = cfg.tls.verify_peer;

    // Patch connection section
    data["connection"]["auto_reconnect"] = cfg.connection.auto_reconnect;
    data["connection"]["reconnect_delay_sec"] = cfg.connection.reconnect_delay_sec;
    data["connection"]["timeout_sec"] = cfg.connection.timeout_sec;

    // Patch notifications section
    data["notifications"]["desktop_notifications"] = cfg.notifications.desktop_notifications;
    data["notifications"]["sound_alerts"] = cfg.notifications.sound_alerts;
    data["notifications"]["notify_on_mention"] = cfg.notifications.notify_on_mention;
    data["notifications"]["notify_on_dm"] = cfg.notifications.notify_on_dm;
    data["notifications"]["mention_keywords"] = cfg.notifications.mention_keywords;

    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream ofs(path);
        ofs << data;
        spdlog::info("Config saved to: {}", path.string());
    } catch (const std::exception& e) {
        spdlog::error("Failed to save config '{}': {}", path.string(), e.what());
    }
}

void export_settings(const ClientConfig& cfg, const std::filesystem::path& path) {
    try {
        toml::value data;
        
        // Export all sections except sensitive identity data
        data["server"]["host"] = cfg.server.host;
        data["server"]["port"] = cfg.server.port;
        
        data["ui"]["theme"] = cfg.ui.theme;
        data["ui"]["timestamp_format"] = cfg.ui.timestamp_format;
        data["ui"]["max_messages"] = cfg.ui.max_messages;
        data["ui"]["font_scale"] = cfg.ui.font_scale;
        data["ui"]["show_timestamps"] = cfg.ui.show_timestamps;
        data["ui"]["show_user_colors"] = cfg.ui.show_user_colors;
        data["ui"]["user_list_width"] = cfg.ui.user_list_width;
        data["ui"]["user_list_collapsed"] = cfg.ui.user_list_collapsed;
        
        data["voice"]["input_device"] = cfg.voice.input_device;
        data["voice"]["output_device"] = cfg.voice.output_device;
        data["voice"]["opus_bitrate"] = cfg.voice.opus_bitrate;
        data["voice"]["frame_ms"] = cfg.voice.frame_ms;
        data["voice"]["mode"] = cfg.voice.mode;
        data["voice"]["ptt_key"] = cfg.voice.ptt_key;
        data["voice"]["vad_threshold"] = cfg.voice.vad_threshold;
        toml::array voice_ice_servers;
        for (const auto& server : cfg.voice.ice_servers) {
            voice_ice_servers.push_back(server);
        }
        data["voice"]["ice_servers"] = voice_ice_servers;
        data["voice"]["turn_username"] = cfg.voice.turn_username;
        data["voice"]["turn_password"] = cfg.voice.turn_password;
        
        data["preview"]["enabled"] = cfg.preview.enabled;
        data["preview"]["fetch_timeout"] = cfg.preview.fetch_timeout;
        data["preview"]["max_cache"] = cfg.preview.max_cache;
        
        data["tls"]["verify_peer"] = cfg.tls.verify_peer;
        
        data["connection"]["auto_reconnect"] = cfg.connection.auto_reconnect;
        data["connection"]["reconnect_delay_sec"] = cfg.connection.reconnect_delay_sec;
        data["connection"]["timeout_sec"] = cfg.connection.timeout_sec;
        
        data["notifications"]["desktop_notifications"] = cfg.notifications.desktop_notifications;
        data["notifications"]["sound_alerts"] = cfg.notifications.sound_alerts;
        data["notifications"]["notify_on_mention"] = cfg.notifications.notify_on_mention;
        data["notifications"]["notify_on_dm"] = cfg.notifications.notify_on_dm;
        data["notifications"]["mention_keywords"] = cfg.notifications.mention_keywords;
        
        std::filesystem::create_directories(path.parent_path());
        std::ofstream ofs(path);
        ofs << data;
        
        spdlog::info("Settings exported to: {}", path.string());
    } catch (const std::exception& e) {
        spdlog::error("Failed to export settings: {}", e.what());
    }
}

bool import_settings(ClientConfig& cfg, const std::filesystem::path& path) {
    try {
        if (!std::filesystem::exists(path)) {
            spdlog::warn("Import file not found: {}", path.string());
            return false;
        }
        
        auto data = toml::parse(path.string());
        
        // Import UI settings
        if (data.contains("ui")) {
            auto& ui = data.at("ui");
            if (ui.contains("theme")) cfg.ui.theme = toml::find<std::string>(ui, "theme");
            if (ui.contains("timestamp_format")) cfg.ui.timestamp_format = toml::find<std::string>(ui, "timestamp_format");
            if (ui.contains("max_messages")) cfg.ui.max_messages = toml::find<int>(ui, "max_messages");
            if (ui.contains("font_scale")) cfg.ui.font_scale = toml::find<int>(ui, "font_scale");
            if (ui.contains("show_timestamps")) cfg.ui.show_timestamps = toml::find<bool>(ui, "show_timestamps");
            if (ui.contains("show_user_colors")) cfg.ui.show_user_colors = toml::find<bool>(ui, "show_user_colors");
            if (ui.contains("user_list_width")) cfg.ui.user_list_width = toml::find<int>(ui, "user_list_width");
            if (ui.contains("user_list_collapsed")) {
                cfg.ui.user_list_collapsed = toml::find<bool>(ui, "user_list_collapsed");
            }
        }
        
        // Import voice settings
        if (data.contains("voice")) {
            auto& v = data.at("voice");
            if (v.contains("input_device")) cfg.voice.input_device = toml::find<std::string>(v, "input_device");
            if (v.contains("output_device")) cfg.voice.output_device = toml::find<std::string>(v, "output_device");
            if (v.contains("opus_bitrate")) cfg.voice.opus_bitrate = toml::find<int>(v, "opus_bitrate");
            if (v.contains("frame_ms")) cfg.voice.frame_ms = toml::find<int>(v, "frame_ms");
            if (v.contains("mode")) cfg.voice.mode = toml::find<std::string>(v, "mode");
            if (v.contains("ptt_key")) cfg.voice.ptt_key = toml::find<std::string>(v, "ptt_key");
            if (v.contains("vad_threshold")) cfg.voice.vad_threshold = static_cast<float>(toml::find<double>(v, "vad_threshold"));
            if (v.contains("ice_servers")) cfg.voice.ice_servers = toml::find<std::vector<std::string>>(v, "ice_servers");
            if (v.contains("turn_username")) cfg.voice.turn_username = toml::find<std::string>(v, "turn_username");
            if (v.contains("turn_password")) cfg.voice.turn_password = toml::find<std::string>(v, "turn_password");
        }
        
        // Import preview settings
        if (data.contains("preview")) {
            auto& p = data.at("preview");
            if (p.contains("enabled")) cfg.preview.enabled = toml::find<bool>(p, "enabled");
            if (p.contains("fetch_timeout")) cfg.preview.fetch_timeout = toml::find<int>(p, "fetch_timeout");
            if (p.contains("max_cache")) cfg.preview.max_cache = toml::find<int>(p, "max_cache");
        }
        
        // Import TLS settings
        if (data.contains("tls")) {
            auto& t = data.at("tls");
            if (t.contains("verify_peer")) cfg.tls.verify_peer = toml::find<bool>(t, "verify_peer");
        }
        
        // Import connection settings
        if (data.contains("connection")) {
            auto& c = data.at("connection");
            if (c.contains("auto_reconnect")) cfg.connection.auto_reconnect = toml::find<bool>(c, "auto_reconnect");
            if (c.contains("reconnect_delay_sec")) cfg.connection.reconnect_delay_sec = toml::find<int>(c, "reconnect_delay_sec");
            if (c.contains("timeout_sec")) cfg.connection.timeout_sec = toml::find<int>(c, "timeout_sec");
        }
        
        // Import notification settings
        if (data.contains("notifications")) {
            auto& n = data.at("notifications");
            if (n.contains("desktop_notifications")) cfg.notifications.desktop_notifications = toml::find<bool>(n, "desktop_notifications");
            if (n.contains("sound_alerts")) cfg.notifications.sound_alerts = toml::find<bool>(n, "sound_alerts");
            if (n.contains("notify_on_mention")) cfg.notifications.notify_on_mention = toml::find<bool>(n, "notify_on_mention");
            if (n.contains("notify_on_dm")) cfg.notifications.notify_on_dm = toml::find<bool>(n, "notify_on_dm");
            if (n.contains("mention_keywords")) cfg.notifications.mention_keywords = toml::find<std::string>(n, "mention_keywords");
        }
        
        spdlog::info("Settings imported from: {}", path.string());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to import settings: {}", e.what());
        return false;
    }
}

} // namespace ircord
