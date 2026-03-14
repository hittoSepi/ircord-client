#pragma once

#include "config.hpp"
#include "state/app_state.hpp"
#include "ui/ui_manager.hpp"
#include "net/net_client.hpp"
#include "net/message_handler.hpp"
#include "crypto/crypto_engine.hpp"
#include "voice/voice_engine.hpp"
#include "preview/link_previewer.hpp"
#include "db/local_store.hpp"
#include "input/command_parser.hpp"
#include "help/help_manager.hpp"

#include <boost/asio/io_context.hpp>
#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ircord {

class App {
public:
    App();
    ~App();

    bool init(const std::filesystem::path& config_path,
              const std::string& user_id_override = {},
              const std::optional<std::pair<std::string, uint16_t>>& server_url = std::nullopt);

    // Runs FTXUI event loop on main thread (blocks until quit).
    int run();

private:
    // Called by UIManager when user presses Enter
    void on_submit(const std::string& line);
    void handle_command(const ParsedCommand& cmd);
    void send_chat(const std::string& text);
    void switch_channel(int delta);
    void switch_to_channel(const std::string& channel_id);
    // Called by UIManager on Alt+1..9; index is 0-based into sorted channel list.
    void switch_to_channel_by_index(int index);
    void persist_message(const std::string& channel_id, const Message& msg);
    void log_system_message(const std::string& channel_id,
                            const std::string& text,
                            bool activate_channel = false);
    void log_server_event(const std::string& text, bool activate_server = false);
    void trace_connection_phase(const std::string& phase,
                                bool reset_attempt_timer = false,
                                bool activate_server = false);
    void handle_command_response(const CommandResponse& response);
    void clear_pending_channel_commands();
    bool has_pending_command(const std::deque<std::string>& queue,
                             const std::string& target) const;

    // Open settings screen
    void open_settings();
    
    // Get public key hex for display in settings
    std::string get_public_key_hex() const;
    
    // Apply theme change immediately
    void on_theme_changed(const std::string& theme_name);
    
    // Save config to file
    void save_current_config();

    ClientConfig cfg_;
    std::filesystem::path config_path_;
    std::string server_version_ = "unknown (server does not advertise version yet)";

    std::unique_ptr<db::LocalStore>         store_;
    AppState                                state_;
    std::unique_ptr<ui::UIManager>          ui_;
    std::unique_ptr<crypto::CryptoEngine>   crypto_;
    std::unique_ptr<voice::VoiceEngine>     voice_;
    std::unique_ptr<LinkPreviewer>          previewer_;
    std::unique_ptr<HelpManager>            help_;

    boost::asio::io_context                 ioc_;
    std::shared_ptr<net::NetClient>         net_client_;
    std::unique_ptr<net::MessageHandler>    msg_handler_;

    std::thread io_thread_;

    mutable std::mutex pending_command_mu_;
    std::deque<std::string> pending_joins_;
    std::deque<std::string> pending_parts_;

    mutable std::mutex connection_trace_mu_;
    std::chrono::steady_clock::time_point connection_attempt_started_{};
    bool has_connection_attempt_ = false;
    
    // Flag to indicate if app should exit (for settings logout)
    bool should_exit_ = false;
};

} // namespace ircord
