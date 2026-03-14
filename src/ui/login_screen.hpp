#pragma once

#include "config.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <functional>
#include <optional>
#include <string>

namespace ircord::ui {

// Login credentials structure
struct LoginCredentials {
    std::string host;
    uint16_t    port = 6697;
    std::string username;
    std::string passkey;
    bool        remember = false;
};

// Result of the login attempt
enum class LoginResult {
    Success,    // User submitted valid credentials
    Cancelled,  // User quit/escaped
};

// LoginScreen handles the centered login form UI.
// It blocks until the user connects or cancels.
class LoginScreen {
public:
    LoginScreen();

    // Run the login screen modally. Returns the result and fills out credentials.
    // Pre-fills with any existing config values.
    LoginResult show(const ClientConfig& existing_cfg,
                     ftxui::ScreenInteractive& screen,
                     LoginCredentials& out_creds,
                     const std::optional<LoginCredentials>& prefill = std::nullopt);

    // Set an error message to display (call from connection callback)
    void set_error(const std::string& error);

    // Set loading state during connection attempt
    void set_loading(bool loading);

private:
    bool validate_inputs();

    // Input values
    std::string host_;
    std::string port_str_;
    std::string username_;
    std::string passkey_;
    bool        remember_ = false;

    // UI state
    std::string error_message_;
    bool        is_loading_ = false;
    bool        submitted_ = false;
    bool        cancelled_ = false;

    // Components
    ftxui::Component host_input_;
    ftxui::Component port_input_;
    ftxui::Component username_input_;
    ftxui::Component passkey_input_;
    ftxui::Component remember_checkbox_;
    ftxui::Component connect_button_;
    ftxui::Component quit_button_;
    ftxui::Component container_;
};

} // namespace ircord::ui
