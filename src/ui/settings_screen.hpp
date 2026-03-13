#pragma once

#include "config.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <functional>
#include <string>

namespace ircord::ui {

// Settings categories
enum class SettingsCategory {
    Appearance,
    Connection,
    Notifications,
    Account
};

// Result of the settings session
enum class SettingsResult {
    Saved,      // User saved changes
    Cancelled,  // User cancelled without saving
    Logout,     // User chose to logout
};

// Callback for theme changes (applied immediately)
using ThemeChangeFn = std::function<void(const std::string& theme_name)>;

// SettingsScreen handles the settings UI with category sidebar.
// It blocks until the user saves, cancels, or logs out.
class SettingsScreen {
public:
    SettingsScreen();

    // Run the settings screen modally. Returns the result.
    // Updates the provided config with any changes made.
    SettingsResult show(ClientConfig& cfg,
                        ftxui::ScreenInteractive& screen,
                        const std::string& public_key_hex,
                        ThemeChangeFn on_theme_change = nullptr);

private:
    void build_ui();
    void save_settings_to_config(ClientConfig& cfg);
    void load_settings_from_config(const ClientConfig& cfg);
    void reset_to_defaults();
    void export_settings();
    void import_settings();
    
    // Category renderers
    ftxui::Element render_appearance();
    ftxui::Element render_connection();
    ftxui::Element render_notifications();
    ftxui::Element render_account();

    // UI State
    SettingsCategory active_category_ = SettingsCategory::Appearance;
    bool saved_ = false;
    bool cancelled_ = false;
    bool logout_ = false;
    
    // Original config for cancel support
    ClientConfig original_cfg_;
    
    // Theme change callback
    ThemeChangeFn on_theme_change_;
    
    // Config paths for import/export
    std::filesystem::path config_path_;

    // === Appearance Settings ===
    std::string theme_;
    int font_scale_;
    bool show_timestamps_;
    bool show_user_colors_;
    std::string timestamp_format_;
    std::string max_messages_;

    // === Connection Settings ===
    bool auto_reconnect_;
    int reconnect_delay_sec_;
    int connection_timeout_sec_;
    bool tls_verify_peer_;
    bool tls_use_custom_cert_;
    std::string tls_cert_pin_;

    // === Notification Settings ===
    bool desktop_notifications_;
    bool sound_alerts_;
    bool notify_on_mention_;
    bool notify_on_dm_;
    std::string mention_keywords_;

    // === Account Settings ===
    std::string nickname_;
    std::string public_key_hex_;

    // Components
    ftxui::Component container_;
    ftxui::Component sidebar_container_;
    ftxui::Component content_container_;
    ftxui::Component save_button_;
    ftxui::Component cancel_button_;
    ftxui::Component reset_button_;
    ftxui::Component export_button_;
    ftxui::Component import_button_;
    ftxui::Component logout_button_;
    
    // Input components for each category
    ftxui::Component theme_input_;
    ftxui::Component timestamp_format_input_;
    ftxui::Component max_messages_input_;
    ftxui::Component reconnect_delay_input_;
    ftxui::Component timeout_input_;
    ftxui::Component cert_pin_input_;
    ftxui::Component keywords_input_;
    ftxui::Component nickname_input_;
};

// Get available theme names
const std::vector<std::string>& available_themes();

// Apply theme by name (updates color scheme)
void apply_theme(const std::string& theme_name);

} // namespace ircord::ui
