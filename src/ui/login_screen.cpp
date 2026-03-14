#include "ui/login_screen.hpp"
#include "ui/color_scheme.hpp"
#include "version.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace ftxui;

namespace ircord::ui {

namespace {

// File paths for secure credential storage
std::filesystem::path get_creds_path() {
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

// Simple XOR encryption for remembered credentials (obfuscation, not high security)
// In production, this should use proper keychain/os credential APIs
std::string encrypt_simple(const std::string& data, const std::string& key) {
    std::string result;
    result.reserve(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        result.push_back(data[i] ^ key[i % key.size()]);
    }
    return result;
}

std::string decrypt_simple(const std::string& data, const std::string& key) {
    return encrypt_simple(data, key); // XOR is symmetric
}

// Generate a simple machine-specific key
std::string get_machine_key() {
    // Use a combination of machine-specific values
    std::string key = "ircord_v1_";
#ifdef _WIN32
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) key += userprofile;
#else
    const char* home = std::getenv("HOME");
    if (home) key += home;
#endif
    // Pad or truncate to 32 chars
    if (key.size() < 32) {
        key.append(32 - key.size(), '0');
    } else if (key.size() > 32) {
        key = key.substr(0, 32);
    }
    return key;
}

} // anonymous namespace

LoginScreen::LoginScreen() = default;

LoginResult LoginScreen::show(const ClientConfig& existing_cfg,
                              ftxui::ScreenInteractive& screen,
                              LoginCredentials& out_creds,
                              const std::optional<LoginCredentials>& prefill) {
    submitted_ = false;
    cancelled_ = false;
    is_loading_ = false;
    error_message_.clear();

    // Pre-fill with existing config values
    host_ = existing_cfg.server.host;
    if (host_.empty() || host_ == "localhost") {
        host_ = "chat.rausku.com"; // Default suggested host
    }
    port_str_ = std::to_string(existing_cfg.server.port);
    if (port_str_ == "6667") {
        port_str_ = "6697"; // Default secure port
    }
    username_ = existing_cfg.identity.user_id;
    if (username_ == "user") {
        username_.clear();
    }

    // Try to load remembered credentials
    try {
        auto creds_path = get_creds_path();
        if (std::filesystem::exists(creds_path)) {
            std::ifstream file(creds_path, std::ios::binary);
            if (file) {
                std::string encrypted((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());
                std::string decrypted = decrypt_simple(encrypted, get_machine_key());
                
                // Parse format: host\nport\nusername\npasskey
                size_t pos = 0;
                std::vector<std::string> parts;
                while (pos < decrypted.size()) {
                    size_t end = decrypted.find('\n', pos);
                    if (end == std::string::npos) end = decrypted.size();
                    parts.push_back(decrypted.substr(pos, end - pos));
                    pos = end + 1;
                }
                
                if (parts.size() >= 4) {
                    host_ = parts[0];
                    port_str_ = parts[1];
                    username_ = parts[2];
                    passkey_ = parts[3];
                    remember_ = true;
                }
            }
        }
    } catch (...) {
        // Ignore load errors
    }

    if (prefill) {
        if (!prefill->host.empty()) {
            host_ = prefill->host;
        }
        if (prefill->port != 0) {
            port_str_ = std::to_string(prefill->port);
        }
        if (!prefill->username.empty()) {
            username_ = prefill->username;
        }
        if (!prefill->passkey.empty()) {
            passkey_ = prefill->passkey;
        }
        remember_ = remember_ || prefill->remember;
    }

    // Get exit closure before building UI
    screen.ForceHandleCtrlC(false);
    auto exit_closure = screen.ExitLoopClosure();

    host_input_ = Input(&host_, "chat.rausku.com");
    port_input_ = Input(&port_str_, "6697");
    username_input_ = Input(&username_, "username");
    passkey_input_ = Input(&passkey_, "passkey");
    remember_checkbox_ = Checkbox(" Remember credentials", &remember_);

    // Connect button callback - exits the loop on valid submit
    connect_button_ = Button("[ CONNECT ]", [this, exit_closure] {
        if (validate_inputs()) {
            submitted_ = true;
            exit_closure();
        }
    });
    quit_button_ = Button("[ QUIT ]", [this, exit_closure] {
        cancelled_ = true;
        exit_closure();
    });

    // Update container with new button
    container_ = Container::Vertical({
        host_input_,
        port_input_,
        username_input_,
        passkey_input_,
        remember_checkbox_,
        connect_button_,
        quit_button_,
    });

    // Build the UI with event handling
    auto base_renderer = Renderer(container_, [this] {
        constexpr int kInputWidth = 32;
        constexpr int kFormWidth = 52;

        // Title
        auto title = text(std::string("IRCord v. ") + std::string(ircord::VERSION)) |
                     bold | color(palette::blue()) | center;

        // Build the form
        auto form = vbox({
            hbox({
                text("HOST:     ") | color(palette::comment()),
                host_input_->Render() | size(WIDTH, EQUAL, kInputWidth) | border,
            }),
            text(""),
            hbox({
                text("PORT:     ") | color(palette::comment()),
                port_input_->Render() | size(WIDTH, EQUAL, kInputWidth) | border,
            }),
            separator(),
            hbox({
                text("USERNAME: ") | color(palette::comment()),
                username_input_->Render() | size(WIDTH, EQUAL, kInputWidth) | border,
            }),
            text(""),
            hbox({
                text("PASSKEY:  ") | color(palette::comment()),
                passkey_input_->Render() | size(WIDTH, EQUAL, kInputWidth) | border,
            }),
            text(""),
            hbox({
                remember_checkbox_->Render(),
                filler(),
                quit_button_->Render(),
                text(" "),
                connect_button_->Render() | (is_loading_ ? dim : nothing),
            }) | hcenter,
        }) | size(WIDTH, EQUAL, kFormWidth) | border | center;

        // Error message
        Element error_el;
        if (!error_message_.empty()) {
            error_el = text("Error: " + error_message_) | color(Color::Red) | center;
        } else {
            error_el = text("") | center;
        }

        // Loading indicator
        Element loading_el;
        if (is_loading_) {
            loading_el = text("Connecting...") | color(palette::cyan()) | blink | center;
        } else {
            loading_el = text("") | center;
        }

        // Main layout - centered form with title and status
        return vbox({
            filler(),
            title,
            text("") | size(HEIGHT, EQUAL, 1),
            form,
            text("") | size(HEIGHT, EQUAL, 1),
            error_el,
            loading_el,
            filler(),
        }) | bgcolor(palette::bg()) | color(palette::fg());
    });

    // Event handler for Escape and Enter
    auto component = CatchEvent(base_renderer, [this, exit_closure](Event event) -> bool {
        if (event.input() == "\x03" || event.input() == "\x04") {
            return true;
        }
        if (event == Event::Escape) {
            cancelled_ = true;
            exit_closure();
            return true;
        }
        if (event == Event::Return && !is_loading_) {
            // Enter key submits the form
            if (validate_inputs()) {
                submitted_ = true;
                exit_closure();
                return true;
            }
        }
        return false;
    });

    // Run the login screen
    screen.Loop(component);

    if (cancelled_) {
        return LoginResult::Cancelled;
    }

    if (submitted_ && validate_inputs()) {
        // Fill output credentials
        out_creds.host = host_;
        out_creds.port = static_cast<uint16_t>(std::atoi(port_str_.c_str()));
        out_creds.username = username_;
        out_creds.passkey = passkey_;
        out_creds.remember = remember_;

        // Save credentials if remember is checked
        if (remember_) {
            try {
                auto creds_path = get_creds_path();
                std::filesystem::create_directories(creds_path.parent_path());
                
                std::string data = host_ + "\n" + port_str_ + "\n" + username_ + "\n" + passkey_;
                std::string encrypted = encrypt_simple(data, get_machine_key());
                
                std::ofstream file(creds_path, std::ios::binary);
                if (file) {
                    file.write(encrypted.data(), encrypted.size());
                }
            } catch (const std::exception& e) {
                spdlog::warn("Failed to save credentials: {}", e.what());
            }
        } else {
            // Delete saved credentials if remember is unchecked
            try {
                auto creds_path = get_creds_path();
                if (std::filesystem::exists(creds_path)) {
                    std::filesystem::remove(creds_path);
                }
            } catch (...) {
                // Ignore deletion errors
            }
        }

        return LoginResult::Success;
    }

    return LoginResult::Cancelled;
}

void LoginScreen::set_error(const std::string& error) {
    error_message_ = error;
}

void LoginScreen::set_loading(bool loading) {
    is_loading_ = loading;
}

// build_ui() implementation is inlined in show() for proper exit handling

bool LoginScreen::validate_inputs() {
    error_message_.clear();

    if (host_.empty()) {
        error_message_ = "Host is required";
        return false;
    }

    if (port_str_.empty()) {
        error_message_ = "Port is required";
        return false;
    }

    // Validate port is a number
    try {
        int port = std::atoi(port_str_.c_str());
        if (port <= 0 || port > 65535) {
            error_message_ = "Port must be between 1 and 65535";
            return false;
        }
    } catch (...) {
        error_message_ = "Port must be a valid number";
        return false;
    }

    if (username_.empty()) {
        error_message_ = "Username is required";
        return false;
    }

    if (passkey_.empty()) {
        error_message_ = "Passkey is required";
        return false;
    }

    return true;
}

} // namespace ircord::ui
