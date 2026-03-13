#include "ui/settings_screen.hpp"
#include "ui/color_scheme.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>
#include <toml.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>

using namespace ftxui;

namespace ircord::ui {

namespace {

// Available themes
const std::vector<std::string> kThemes = {
    "tokyo-night",
    "dark",
    "light",
    "dracula",
    "nord",
    "solarized-dark",
    "solarized-light"
};

// Default config values
constexpr bool kDefaultAutoReconnect = true;
constexpr int kDefaultReconnectDelay = 5;
constexpr int kDefaultTimeout = 30;
constexpr bool kDefaultTlsVerify = true;
constexpr bool kDefaultDesktopNotifications = true;
constexpr bool kDefaultSoundAlerts = true;
constexpr bool kDefaultNotifyMention = true;
constexpr bool kDefaultNotifyDM = true;
constexpr bool kDefaultShowTimestamps = true;
constexpr bool kDefaultShowUserColors = true;
constexpr int kDefaultFontScale = 100;
constexpr int kDefaultMaxMessages = 1000;

// Category labels
std::string category_label(SettingsCategory cat) {
    switch (cat) {
        case SettingsCategory::Appearance: return "Appearance";
        case SettingsCategory::Connection: return "Connection";
        case SettingsCategory::Notifications: return "Notifications";
        case SettingsCategory::Account: return "Account";
    }
    return "Unknown";
}

// Category icon
std::string category_icon(SettingsCategory cat) {
    switch (cat) {
        case SettingsCategory::Appearance: return "\xf0\x9f\x8e\xa8"; // Palette
        case SettingsCategory::Connection: return "\xe2\x9a\xa1";     // Lightning
        case SettingsCategory::Notifications: return "\xf0\x9f\x94\x94"; // Bell
        case SettingsCategory::Account: return "\xf0\x9f\x91\xa4";     // Person
    }
    return "";
}

} // anonymous namespace

const std::vector<std::string>& available_themes() {
    return kThemes;
}

void apply_theme(const std::string& theme_name) {
    // Theme is applied through color_scheme.hpp - colors are currently static
    // In a full implementation, this would update palette colors dynamically
    spdlog::info("Theme changed to: {}", theme_name);
}

SettingsScreen::SettingsScreen() = default;

SettingsResult SettingsScreen::show(ClientConfig& cfg,
                                    ftxui::ScreenInteractive& screen,
                                    const std::string& public_key_hex,
                                    ThemeChangeFn on_theme_change) {
    original_cfg_ = cfg;
    public_key_hex_ = public_key_hex;
    on_theme_change_ = on_theme_change;
    config_path_ = cfg.config_dir / "config.toml";
    
    load_settings_from_config(cfg);
    build_ui();
    
    auto exit_closure = screen.ExitLoopClosure();
    
    // Main renderer
    auto renderer = Renderer(container_, [this] {
        // Sidebar
        auto sidebar = vbox({
            text(" Settings ") | bold | color(palette::blue()),
            separator(),
            sidebar_container_->Render(),
        }) | border | size(WIDTH, LESS_THAN, 20);
        
        // Content area based on active category
        Element content;
        switch (active_category_) {
            case SettingsCategory::Appearance:
                content = render_appearance();
                break;
            case SettingsCategory::Connection:
                content = render_connection();
                break;
            case SettingsCategory::Notifications:
                content = render_notifications();
                break;
            case SettingsCategory::Account:
                content = render_account();
                break;
        }
        
        // Action buttons at bottom
        auto actions = hbox({
            save_button_->Render(),
            text(" "),
            cancel_button_->Render(),
            filler(),
            reset_button_->Render(),
        });
        
        auto main_content = vbox({
            text(" " + category_label(active_category_) + " ") | bold | color(palette::blue()),
            separator(),
            content | flex,
            separator(),
            actions,
        }) | border | flex;
        
        return hbox({
            sidebar,
            main_content,
        }) | bgcolor(palette::bg()) | color(palette::fg());
    });
    
    // Event handler
    auto component = CatchEvent(renderer, [this, exit_closure, &cfg](Event event) -> bool {
        if (event == Event::Escape) {
            cancelled_ = true;
            exit_closure();
            return true;
        }
        if (event == Event::Character("q") || event == Event::Character("Q")) {
            cancelled_ = true;
            exit_closure();
            return true;
        }
        // F1-F4 to switch categories
        if (event == Event::F1) {
            active_category_ = SettingsCategory::Appearance;
            return true;
        }
        if (event == Event::F2) {
            active_category_ = SettingsCategory::Connection;
            return true;
        }
        if (event == Event::F3) {
            active_category_ = SettingsCategory::Notifications;
            return true;
        }
        if (event == Event::F4) {
            active_category_ = SettingsCategory::Account;
            return true;
        }
        return false;
    });
    
    screen.Loop(component);
    
    if (logout_) {
        return SettingsResult::Logout;
    }
    if (cancelled_) {
        cfg = original_cfg_; // Restore original config
        return SettingsResult::Cancelled;
    }
    if (saved_) {
        save_settings_to_config(cfg);
        return SettingsResult::Saved;
    }
    return SettingsResult::Cancelled;
}

void SettingsScreen::build_ui() {
    // Sidebar category buttons
    sidebar_container_ = Container::Vertical({
        Button(" Appearance ", [this] { active_category_ = SettingsCategory::Appearance; }),
        Button(" Connection ", [this] { active_category_ = SettingsCategory::Connection; }),
        Button(" Notifications ", [this] { active_category_ = SettingsCategory::Notifications; }),
        Button(" Account ", [this] { active_category_ = SettingsCategory::Account; }),
    });
    
    // Input components
    theme_input_ = Input(&theme_, "tokyo-night");
    timestamp_format_input_ = Input(&timestamp_format_, "%H:%M");
    max_messages_input_ = Input(&max_messages_, "1000");
    reconnect_delay_input_ = Input(std::to_string(reconnect_delay_sec_), "5");
    timeout_input_ = Input(std::to_string(connection_timeout_sec_), "30");
    cert_pin_input_ = Input(&tls_cert_pin_, "");
    keywords_input_ = Input(&mention_keywords_, "");
    nickname_input_ = Input(&nickname_, "");
    
    // Action buttons
    save_button_ = Button("[ SAVE ]", [this] {
        saved_ = true;
    });
    cancel_button_ = Button("[ CANCEL ]", [this] {
        cancelled_ = true;
    });
    reset_button_ = Button("[ Reset to Defaults ]", [this] {
        reset_to_defaults();
    });
    
    // Main container - vertical layout of sidebar and content containers
    container_ = Container::Vertical({
        sidebar_container_,
        theme_input_,
        timestamp_format_input_,
        max_messages_input_,
        reconnect_delay_input_,
        timeout_input_,
        cert_pin_input_,
        keywords_input_,
        nickname_input_,
        save_button_,
        cancel_button_,
        reset_button_,
    });
}

Element SettingsScreen::render_appearance() {
    // Theme selection dropdown (using input for now)
    auto theme_row = hbox({
        text("Theme: ") | color(palette::fg_dark()),
        theme_input_->Render() | size(WIDTH, GREATER_THAN, 20) | border,
        text(" (" + std::to_string(kThemes.size()) + " available)" ) | color(palette::comment()),
    });
    
    // Font scale slider representation
    auto font_row = hbox({
        text("Font Scale: ") | color(palette::fg_dark()),
        text(std::to_string(font_scale_) + "%") | color(palette::cyan()),
    });
    
    // Display options
    auto timestamp_checkbox = Checkbox("Show timestamps", &show_timestamps_);
    auto user_colors_checkbox = Checkbox("Colorize usernames", &show_user_colors_);
    
    // Timestamp format
    auto format_row = hbox({
        text("Timestamp Format: ") | color(palette::fg_dark()),
        timestamp_format_input_->Render() | size(WIDTH, GREATER_THAN, 15) | border,
    });
    
    // Max messages
    auto max_msg_row = hbox({
        text("Max Messages: ") | color(palette::fg_dark()),
        max_messages_input_->Render() | size(WIDTH, GREATER_THAN, 10) | border,
    });
    
    return vbox({
        text("Theme Settings") | bold | color(palette::blue()),
        separator(),
        theme_row,
        text(""),
        text("Display Options") | bold | color(palette::blue()),
        separator(),
        timestamp_checkbox->Render(),
        user_colors_checkbox->Render(),
        format_row,
        max_msg_row,
        text(""),
        text("Note: Theme changes apply immediately. Other changes apply on save.") 
            | color(palette::comment()) | dim,
    });
}

Element SettingsScreen::render_connection() {
    auto reconnect_checkbox = Checkbox("Auto-reconnect on disconnect", &auto_reconnect_);
    
    auto delay_row = hbox({
        text("Reconnect Delay: ") | color(palette::fg_dark()),
        reconnect_delay_input_->Render() | size(WIDTH, GREATER_THAN, 5) | border,
        text(" seconds") | color(palette::comment()),
    });
    
    auto timeout_row = hbox({
        text("Connection Timeout: ") | color(palette::fg_dark()),
        timeout_input_->Render() | size(WIDTH, GREATER_THAN, 5) | border,
        text(" seconds") | color(palette::comment()),
    });
    
    auto verify_checkbox = Checkbox("Verify TLS certificates", &tls_verify_peer_);
    
    auto cert_pin_row = hbox({
        text("Certificate PIN: ") | color(palette::fg_dark()),
        cert_pin_input_->Render() | size(WIDTH, GREATER_THAN, 40) | border,
    });
    
    return vbox({
        text("Reconnect Behavior") | bold | color(palette::blue()),
        separator(),
        reconnect_checkbox->Render(),
        delay_row,
        text(""),
        text("Timeout Settings") | bold | color(palette::blue()),
        separator(),
        timeout_row,
        text(""),
        text("TLS/SSL Options") | bold | color(palette::blue()),
        separator(),
        verify_checkbox->Render(),
        cert_pin_row,
        text(""),
        text("Note: Connection settings take effect on next connection.") 
            | color(palette::comment()) | dim,
    });
}

Element SettingsScreen::render_notifications() {
    auto desktop_checkbox = Checkbox("Enable desktop notifications", &desktop_notifications_);
    auto sound_checkbox = Checkbox("Enable sound alerts", &sound_alerts_);
    auto mention_checkbox = Checkbox("Notify on mentions", &notify_on_mention_);
    auto dm_checkbox = Checkbox("Notify on direct messages", &notify_on_dm_);
    
    auto keywords_row = hbox({
        text("Mention Keywords: ") | color(palette::fg_dark()),
        keywords_input_->Render() | size(WIDTH, GREATER_THAN, 30) | border,
    });
    
    return vbox({
        text("Notification Settings") | bold | color(palette::blue()),
        separator(),
        desktop_checkbox->Render(),
        sound_checkbox->Render(),
        text(""),
        text("Mention Settings") | bold | color(palette::blue()),
        separator(),
        mention_checkbox->Render(),
        dm_checkbox->Render(),
        keywords_row,
        text("  Comma-separated keywords (e.g.: nick1, nick2, word1)") | color(palette::comment()) | dim,
    });
}

Element SettingsScreen::render_account() {
    auto nickname_row = hbox({
        text("Nickname: ") | color(palette::fg_dark()),
        nickname_input_->Render() | size(WIDTH, GREATER_THAN, 25) | border,
    });
    
    // Public key display (read-only)
    std::string pk_display = public_key_hex_.empty() ? "(not available)" : public_key_hex_;
    if (pk_display.size() > 50) {
        pk_display = pk_display.substr(0, 47) + "...";
    }
    
    auto pubkey_row = vbox({
        text("Public Key (Ed25519):") | color(palette::fg_dark()),
        text(pk_display) | color(palette::cyan()),
    });
    
    auto import_export = hbox({
        Button("[ Export Settings ]", [this] { export_settings(); })->Render(),
        text("  "),
        Button("[ Import Settings ]", [this] { import_settings(); })->Render(),
    });
    
    auto danger_zone = vbox({
        text("Danger Zone") | bold | color(palette::red()),
        separator(),
        Button("[ LOGOUT ]", [this] { logout_ = true; })->Render() | color(palette::red()),
    });
    
    return vbox({
        text("Account Settings") | bold | color(palette::blue()),
        separator(),
        nickname_row,
        text("  This nickname will be used for new connections.") | color(palette::comment()) | dim,
        text(""),
        pubkey_row,
        text(""),
        text("Import/Export") | bold | color(palette::blue()),
        separator(),
        import_export,
        text(""),
        danger_zone,
    });
}

void SettingsScreen::load_settings_from_config(const ClientConfig& cfg) {
    // Appearance
    theme_ = cfg.ui.theme;
    timestamp_format_ = cfg.ui.timestamp_format;
    max_messages_ = std::to_string(cfg.ui.max_messages);
    font_scale_ = kDefaultFontScale;
    show_timestamps_ = kDefaultShowTimestamps;
    show_user_colors_ = kDefaultShowUserColors;
    
    // Connection (from existing config or defaults)
    auto_reconnect_ = kDefaultAutoReconnect;
    reconnect_delay_sec_ = (kDefaultReconnectDelay);
    connection_timeout_sec_ = (kDefaultTimeout);
    tls_verify_peer_ = cfg.tls.verify_peer;
    tls_use_custom_cert_ = !cfg.server.cert_pin.empty();
    tls_cert_pin_ = cfg.server.cert_pin;
    
    // Notifications (defaults)
    desktop_notifications_ = kDefaultDesktopNotifications;
    sound_alerts_ = kDefaultSoundAlerts;
    notify_on_mention_ = kDefaultNotifyMention;
    notify_on_dm_ = kDefaultNotifyDM;
    mention_keywords_ = cfg.identity.user_id; // Default to username
    
    // Account
    nickname_ = cfg.identity.user_id;
}

void SettingsScreen::save_settings_to_config(ClientConfig& cfg) {
    // Appearance
    cfg.ui.theme = theme_;
    cfg.ui.timestamp_format = timestamp_format_;
    try {
        cfg.ui.max_messages = std::stoi(max_messages_);
    } catch (...) {
        cfg.ui.max_messages = kDefaultMaxMessages;
    }
    
    // Connection
    cfg.tls.verify_peer = tls_verify_peer_;
    cfg.server.cert_pin = tls_cert_pin_;
    
    // Account
    if (!nickname_.empty() && nickname_ != cfg.identity.user_id) {
        cfg.identity.user_id = nickname_;
    }
    
    // Notify theme change
    if (on_theme_change_ && theme_ != original_cfg_.ui.theme) {
        on_theme_change_(theme_);
    }
}

void SettingsScreen::reset_to_defaults() {
    // Appearance defaults
    theme_ = "tokyo-night";
    font_scale_ = kDefaultFontScale;
    show_timestamps_ = kDefaultShowTimestamps;
    show_user_colors_ = kDefaultShowUserColors;
    timestamp_format_ = "%H:%M";
    max_messages_ = std::to_string(kDefaultMaxMessages);
    
    // Connection defaults
    auto_reconnect_ = kDefaultAutoReconnect;
    reconnect_delay_sec_ = kDefaultReconnectDelay;
    connection_timeout_sec_ = kDefaultTimeout;
    tls_verify_peer_ = kDefaultTlsVerify;
    tls_use_custom_cert_ = false;
    tls_cert_pin_.clear();
    
    // Notification defaults
    desktop_notifications_ = kDefaultDesktopNotifications;
    sound_alerts_ = kDefaultSoundAlerts;
    notify_on_mention_ = kDefaultNotifyMention;
    notify_on_dm_ = kDefaultNotifyDM;
    mention_keywords_ = nickname_;
}

void SettingsScreen::export_settings() {
    try {
        auto export_path = original_cfg_.config_dir / "settings_backup.toml";
        std::filesystem::create_directories(export_path.parent_path());
        
        toml::value data;
        data["ui"]["theme"] = theme_;
        data["ui"]["timestamp_format"] = timestamp_format_;
        data["ui"]["max_messages"] = std::stoi(max_messages_);
        
        data["connection"]["auto_reconnect"] = auto_reconnect_;
        data["connection"]["reconnect_delay"] = reconnect_delay_sec_;
        data["connection"]["timeout"] = connection_timeout_sec_;
        
        data["notifications"]["desktop"] = desktop_notifications_;
        data["notifications"]["sound"] = sound_alerts_;
        data["notifications"]["on_mention"] = notify_on_mention_;
        data["notifications"]["keywords"] = mention_keywords_;
        
        std::ofstream ofs(export_path);
        ofs << data;
        
        spdlog::info("Settings exported to: {}", export_path.string());
    } catch (const std::exception& e) {
        spdlog::error("Failed to export settings: {}", e.what());
    }
}

void SettingsScreen::import_settings() {
    try {
        auto import_path = original_cfg_.config_dir / "settings_backup.toml";
        if (!std::filesystem::exists(import_path)) {
            spdlog::warn("No backup file found at: {}", import_path.string());
            return;
        }
        
        auto data = toml::parse(import_path.string());
        
        if (data.contains("ui")) {
            auto& ui = data.at("ui");
            if (ui.contains("theme")) theme_ = toml::find<std::string>(ui, "theme");
            if (ui.contains("timestamp_format")) timestamp_format_ = toml::find<std::string>(ui, "timestamp_format");
            if (ui.contains("max_messages")) max_messages_ = std::to_string(toml::find<int>(ui, "max_messages"));
        }
        
        if (data.contains("connection")) {
            
            auto& conn = data.at("connection");
            if (conn.contains("auto_reconnect")) auto_reconnect_ = toml::find<bool>(conn, "auto_reconnect");
            if (conn.contains("reconnect_delay")) reconnect_delay_sec_ = std::atoi(std::to_string(toml::find<int>(conn, "reconnect_delay")).c_str());
            if (conn.contains("timeout")) connection_timeout_sec_ = int(toml::find<int>(conn, "timeout"));
        }
        
        if (data.contains("notifications")) {
            auto& notif = data.at("notifications");
            if (notif.contains("desktop")) desktop_notifications_ = toml::find<bool>(notif, "desktop");
            if (notif.contains("sound")) sound_alerts_ = toml::find<bool>(notif, "sound");
            if (notif.contains("on_mention")) notify_on_mention_ = toml::find<bool>(notif, "on_mention");
            if (notif.contains("keywords")) mention_keywords_ = toml::find<std::string>(notif, "keywords");
        }
        
        spdlog::info("Settings imported from: {}", import_path.string());
    } catch (const std::exception& e) {
        spdlog::error("Failed to import settings: {}", e.what());
    }
}

} // namespace ircord::ui
