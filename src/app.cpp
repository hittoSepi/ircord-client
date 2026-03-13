#include "app.hpp"
#include "input/command_parser.hpp"
#include "ui/login_screen.hpp"
#include "ui/settings_screen.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <sodium.h>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/steady_timer.hpp>
#include <iostream>
#include <chrono>
#include <regex>
#include <thread>
#include <functional>

namespace ircord {

App::App() = default;

App::~App() {
    ioc_.stop();
    if (io_thread_.joinable()) io_thread_.join();
    if (previewer_) previewer_->stop();
}

bool App::init(const std::filesystem::path& config_path,
               const std::string& user_id_override) {
    config_path_ = config_path;
    
    // ── Logging ───────────────────────────────────────────────────────────
    // File sink for debug output (TUI hides stderr)
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        "ircord-debug.log", /*truncate=*/true);
    auto logger = std::make_shared<spdlog::logger>("ircord", file_sink);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::debug);

    // ── libsodium ─────────────────────────────────────────────────────────
    if (sodium_init() < 0) {
        std::cerr << "sodium_init() failed\n";
        return false;
    }

    // ── Config ────────────────────────────────────────────────────────────
    cfg_ = load_config(config_path);
    if (!user_id_override.empty()) cfg_.identity.user_id = user_id_override;

    // ── Login Screen (FTXUI) ──────────────────────────────────────────────
    // Show the graphical login screen to get credentials
    ui::LoginCredentials login_creds;
    {
        ui::LoginScreen login_screen;
        ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
        
        auto result = login_screen.show(cfg_, screen, login_creds);
        
        if (result == ui::LoginResult::Cancelled) {
            spdlog::info("Login cancelled by user");
            return false;
        }
        
        // Update config with login credentials
        cfg_.server.host = login_creds.host;
        cfg_.server.port = login_creds.port;
        cfg_.identity.user_id = login_creds.username;
        
        // Save config with new values
        save_config(cfg_, config_path);
    }

    std::filesystem::create_directories(cfg_.config_dir);

    // ── Database ──────────────────────────────────────────────────────────
    try {
        store_ = std::make_unique<db::LocalStore>(cfg_.db_path);
    } catch (const std::exception& e) {
        std::cerr << "DB open failed: " << e.what() << "\n";
        return false;
    }

    // ── Crypto (use passkey from login screen) ───────────────────────────
    crypto_ = std::make_unique<crypto::CryptoEngine>();

    // Use the passkey entered in the login screen
    const std::string& passphrase = login_creds.passkey;

    if (!crypto_->init(*store_, cfg_, passphrase)) {
        // Show error and fail - the user will need to restart
        std::cerr << "Crypto init failed (wrong passphrase?)\n";
        return false;
    }

    // ── State & UI ────────────────────────────────────────────────────────
    state_.set_local_user_id(cfg_.identity.user_id);
    state_.ensure_channel("server");

    // Wire AppState::post_ui to also wake the FTXUI event loop
    ui_ = std::make_unique<ui::UIManager>(state_, cfg_);
    // We'll wire notify after ui_ is created — AppState doesn't own the callback;
    // instead UIManager::notify() is called explicitly where needed.

    // ── Voice ─────────────────────────────────────────────────────────────
    voice_ = std::make_unique<voice::VoiceEngine>(state_, cfg_);

    // ── Link Previewer ────────────────────────────────────────────────────
    if (cfg_.preview.enabled) {
        previewer_ = std::make_unique<LinkPreviewer>(
            *store_, cfg_.preview.fetch_timeout, cfg_.preview.max_cache);
        previewer_->start();
    }

    // ── Network ───────────────────────────────────────────────────────────
    msg_handler_ = std::make_unique<net::MessageHandler>(state_, *crypto_, cfg_);

    net::NetCallbacks cb;
    cb.on_message = [this](const Envelope& env) {
        msg_handler_->dispatch(env);
        // Wake UI for any received message
        if (ui_) ui_->notify();
    };
    cb.on_connected = [this]() {
        // Send HELLO
        Hello hello;
        hello.set_protocol_version(1);
        hello.set_client_version("ircord-client/0.1.0");
        Envelope env;
        env.set_seq(1);
        env.set_type(MT_HELLO);
        env.set_payload(hello.SerializeAsString());
        net_client_->send(env);
    };
    cb.on_disconnected = [this](const std::string& reason) {
        state_.set_connected(false);
        if (ui_) ui_->push_system_msg("Disconnected: " + reason);
    };

    net_client_ = std::make_shared<net::NetClient>(ioc_, cfg_, std::move(cb));
    msg_handler_->set_net_client(net_client_.get());
    msg_handler_->set_voice_engine(voice_.get());
    msg_handler_->set_persist_callback([this](const std::string& channel_id, const Message& msg) {
        persist_message(channel_id, msg);
    });

    voice_->set_send_signal([this](const VoiceSignal& sig) {
        Envelope env;
        env.set_type(MT_VOICE_SIGNAL);
        env.set_payload(sig.SerializeAsString());
        net_client_->send(env);
    });

    voice_->set_send_room_msg([this](MessageType type, const google::protobuf::Message& msg) {
        Envelope env;
        env.set_type(type);
        msg.SerializeToString(env.mutable_payload());
        net_client_->send(env);
    });

    return true;
}

int App::run() {
    // ── IO thread ─────────────────────────────────────────────────────────
    auto work = boost::asio::make_work_guard(ioc_);
    io_thread_ = std::thread([this]() {
        net_client_->start();
        ioc_.run();
    });

    // ── Speaking indicator refresh timer (100ms) ──────────────────────────
    auto speaking_timer = std::make_shared<boost::asio::steady_timer>(ioc_);
    std::function<void()> refresh_speaking;
    refresh_speaking = [this, speaking_timer, &refresh_speaking]() {
        voice_->refresh_speaking_state();
        speaking_timer->expires_after(std::chrono::milliseconds(100));
        speaking_timer->async_wait([&refresh_speaking](auto) { refresh_speaking(); });
    };
    speaking_timer->expires_after(std::chrono::milliseconds(100));
    speaking_timer->async_wait([&refresh_speaking](auto) { refresh_speaking(); });

    ui_->push_system_msg("IrssiCord v0.1.0 — connecting to " +
                          cfg_.server.host + ":" + std::to_string(cfg_.server.port));

    // ── FTXUI event loop (blocks main thread) ─────────────────────────────
    ui_->run(
        [this](const std::string& line) { on_submit(line); },
        [this]()                         { ioc_.stop(); },
        [this](int idx)                  { switch_to_channel_by_index(idx); },
        [this]()                         { voice_->toggle_voice_mode(); },
        [this]()                         { open_settings(); }
    );

    // ── Shutdown ─────────────────────────────────────────────────────────
    ioc_.stop();
    work.reset();
    if (io_thread_.joinable()) io_thread_.join();
    if (previewer_) previewer_->stop();

    return should_exit_ ? 1 : 0;
}

void App::on_submit(const std::string& line) {
    if (line.empty()) return;

    if (line[0] == '/') {
        auto cmd = parse_command(line);
        if (cmd) handle_command(*cmd);
        else     ui_->push_system_msg("Unknown command: " + line);
    } else {
        send_chat(line);
    }
}

void App::handle_command(const ParsedCommand& cmd) {
    if (cmd.name == "/quit" || cmd.name == "/exit") {
        ioc_.stop();
        // Exit FTXUI loop — post from another thread
        // UIManager will exit when on_quit callback fires; here we trigger it
        // via a system message and let the user press Esc/Ctrl-C
        ui_->push_system_msg("Goodbye.");
        return;
    }
    if (cmd.name == "/settings") {
        open_settings();
        return;
    }
    if (cmd.name == "/part") {
        auto ch = state_.active_channel().value_or("");
        if (ch.empty() || ch == "server") {
            ui_->push_system_msg("Cannot leave the server channel.");
        } else {
            std::string left = ch;          // copy before removal
            state_.remove_channel(left);    // active channel updates first
            ui_->push_system_msg("Left " + left + ".");  // message goes to new active ch
        }
    } else if (cmd.name == "/join" && !cmd.args.empty()) {
        switch_to_channel(cmd.args[0]);
    } else if (cmd.name == "/msg" && cmd.args.size() >= 2) {
        std::string target = cmd.args[0];
        std::string text;
        for (size_t i = 1; i < cmd.args.size(); ++i) {
            if (i > 1) text += ' ';
            text += cmd.args[i];
        }
        state_.ensure_channel(target);
        state_.set_active_channel(target);
        send_chat(text);
    } else if (cmd.name == "/call" && !cmd.args.empty()) {
        voice_->call(cmd.args[0]);
        ui_->push_system_msg("Calling " + cmd.args[0] + "...");
    } else if (cmd.name == "/accept" && !cmd.args.empty()) {
        voice_->accept_call(cmd.args[0]);
        ui_->push_system_msg("Accepted call from " + cmd.args[0]);
    } else if (cmd.name == "/hangup") {
        voice_->hangup();
        ui_->push_system_msg("Call ended.");
    } else if (cmd.name == "/voice") {
        if (!cmd.args.empty() && cmd.args[0] == "leave") {
            voice_->leave_room();
            ui_->push_system_msg("Left voice room.");
        } else if (voice_->in_voice()) {
            voice_->leave_room();
            ui_->push_system_msg("Left voice room.");
        } else {
            std::string ch = cmd.args.empty()
                ? state_.active_channel().value_or("#general")
                : cmd.args[0];
            voice_->join_room(ch);
            ui_->push_system_msg("Joining voice room: " + ch + "...");
        }
    } else if (cmd.name == "/mute") {
        bool m = !state_.voice_snapshot().muted;
        voice_->set_muted(m);
        ui_->push_system_msg(m ? "Muted." : "Unmuted.");
    } else if (cmd.name == "/deafen") {
        bool d = !state_.voice_snapshot().deafened;
        voice_->set_deafened(d);
        ui_->push_system_msg(d ? "Deafened." : "Undeafened.");
    } else if (cmd.name == "/ptt") {
        voice_->toggle_voice_mode();
        ui_->push_system_msg("Voice mode: " + voice_->voice_mode());
    } else if (cmd.name == "/trust" && !cmd.args.empty()) {
        std::string peer = cmd.args[0];
        std::string sn   = crypto_->safety_number(peer, *store_);
        ui_->push_system_msg("Safety number with " + peer + ":");
        ui_->push_system_msg("  " + sn);
        auto existing = store_->load_peer_identity(peer);
        if (existing) store_->save_peer_identity(peer, *existing, "verified");
    } else if (cmd.name == "/me" && !cmd.args.empty()) {
        // Action message (/me eats pizza)
        std::vector<std::string> action_args;
        std::string action;
        for (size_t i = 0; i < cmd.args.size(); ++i) {
            if (i > 0) action += ' ';
            action += cmd.args[i];
        }
        action_args.push_back(action);
        auto ch = state_.active_channel().value_or("#general");
        // Send action as IRC command
        if (msg_handler_) {
            msg_handler_->send_command("me", action_args);
        }
        // Show locally too
        const std::string& local_id = cfg_.identity.user_id;
        Message action_msg;
        action_msg.sender_id    = local_id;
        action_msg.content      = "* " + local_id + " " + action;
        action_msg.type         = Message::Type::System;
        action_msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        state_.push_message(ch, std::move(action_msg));
        ui_->notify();
    } else if (cmd.name == "/names") {
        auto users = state_.online_users();
        std::string list;
        for (auto& u : users) list += u + " ";
        ui_->push_system_msg("Online: " + (list.empty() ? "(none)" : list));
    } else if (cmd.name == "/search" && !cmd.args.empty()) {
        // Join all args into query string
        std::string query;
        for (size_t i = 0; i < cmd.args.size(); ++i) {
            if (i > 0) query += " ";
            query += cmd.args[i];
        }
        if (store_) {
            db::LocalStore::SearchFilters filters;
            filters.limit = 20;
            auto results = store_->search_messages(query, filters);
            ui_->push_system_msg("Search results for '" + query + "':");
            if (results.empty()) {
                ui_->push_system_msg("  (no results)");
            } else {
                for (const auto& r : results) {
                    std::string preview = r.content;
                    if (preview.length() > 60) preview = preview.substr(0, 57) + "...";
                    // Format: [channel] <sender>: content
                    ui_->push_system_msg("  [" + r.channel_id + "] <" + r.sender_id + ">: " + preview);
                }
            }
        } else {
            ui_->push_system_msg("Search not available (no database).");
        }
    } else if (cmd.name == "/clear") {
        ui_->push_system_msg("(cleared)");
    } else if (cmd.name == "/help") {
        ui_->push_system_msg(
            "Commands: /join /part /msg /me /call /accept /hangup /voice "
            "/mute /deafen /ptt /trust /names /search /clear /settings /quit");
        ui_->push_system_msg(
            "Shortcuts: F1=PTT toggle, F12=Settings, Alt+1-9=Switch channel, Esc=Quit");
    } else {
        ui_->push_system_msg("Unknown command: " + cmd.name);
    }
    ui_->notify();
}

void App::send_chat(const std::string& text) {
    if (!net_client_->is_connected()) {
        ui_->push_system_msg("Not connected.");
        return;
    }
    auto active = state_.active_channel().value_or("");
    if (active.empty()) {
        ui_->push_system_msg("No active channel.");
        return;
    }
    if (active == "server") {
        ui_->push_system_msg("Cannot send messages here. Use /join #channel or /msg <user>.");
        return;
    }

    const std::string& local_id = cfg_.identity.user_id;

    // Show immediately in local UI
    Message local_msg;
    local_msg.sender_id    = local_id;
    local_msg.content      = text;
    local_msg.type         = Message::Type::Chat;
    local_msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    state_.push_message(active, local_msg);
    ui_->notify();

    // Persist to database for search
    if (store_) {
        db::LocalStore::MessageRow row;
        row.channel_id   = active;
        row.sender_id    = local_id;
        row.content      = text;
        row.timestamp_ms = local_msg.timestamp_ms;
        row.type         = static_cast<int>(Message::Type::Chat);
        store_->save_message(row);
    }

    // Encrypt and send (for DMs with no session yet, msg_handler queues
    // the plaintext and sends KEY_REQUEST; the message is flushed when
    // KEY_BUNDLE arrives in handle_key_bundle).
    auto env = crypto_->encrypt(local_id, active, text,
        [this, text](const std::string& recipient_id) {
            msg_handler_->request_key(recipient_id, text);
        });

    if (!env.sender_id().empty()) {
        Envelope wire;
        wire.set_type(MT_CHAT_ENVELOPE);
        wire.set_payload(env.SerializeAsString());
        net_client_->send(wire);
    }

    // Link preview
    if (previewer_) {
        static const std::regex url_re(R"(https?://\S{3,})");
        std::sregex_iterator it(text.begin(), text.end(), url_re);
        for (; it != std::sregex_iterator{}; ++it) {
            std::string url = (*it)[0].str();
            previewer_->fetch(url, [this, active](PreviewResult r) {
                if (!r.success) return;
                state_.post_ui([this, active, r = std::move(r)]() mutable {
                    Message pm;
                    pm.type      = Message::Type::System;
                    pm.sender_id = "preview";
                    pm.content   = "\u250C " + r.title +
                                   (r.description.empty() ? "" : " \u2014 " + r.description);
                    pm.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    state_.push_message(active, std::move(pm));
                });
                if (ui_) ui_->notify();
            });
        }
    }
}

// Helper to save incoming messages to database
void App::persist_message(const std::string& channel_id, const Message& msg) {
    if (!store_) return;
    db::LocalStore::MessageRow row;
    row.channel_id   = channel_id;
    row.sender_id    = msg.sender_id;
    row.content      = msg.content;
    row.timestamp_ms = msg.timestamp_ms;
    row.type         = static_cast<int>(msg.type);
    store_->save_message(row);
}

void App::switch_channel(int delta) {
    auto channels = state_.channel_list();
    if (channels.empty()) return;
    auto active = state_.active_channel().value_or(channels[0]);
    auto it = std::find(channels.begin(), channels.end(), active);
    int  idx = (it == channels.end()) ? 0 : static_cast<int>(it - channels.begin());
    idx = (idx + delta + static_cast<int>(channels.size())) % static_cast<int>(channels.size());
    switch_to_channel(channels[idx]);
}

void App::switch_to_channel_by_index(int index) {
    auto channels = state_.channel_list();
    if (index < 0 || index >= static_cast<int>(channels.size())) return;
    switch_to_channel(channels[index]);
}

void App::switch_to_channel(const std::string& channel_id) {
    state_.ensure_channel(channel_id);
    state_.set_active_channel(channel_id);
    ui_->push_system_msg("Switched to " + channel_id);
}

void App::open_settings() {
    // Get public key for display
    std::string pubkey_hex = get_public_key_hex();
    
    // Create a new screen for settings (modal)
    ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
    
    ui::SettingsScreen settings_screen;
    auto result = settings_screen.show(cfg_, screen, pubkey_hex,
        [this](const std::string& theme) { on_theme_changed(theme); });
    
    switch (result) {
        case ui::SettingsResult::Saved:
            save_current_config();
            ui_->push_system_msg("Settings saved.");
            break;
        case ui::SettingsResult::Cancelled:
            ui_->push_system_msg("Settings cancelled.");
            break;
        case ui::SettingsResult::Logout:
            ui_->push_system_msg("Logging out...");
            should_exit_ = true;
            // Trigger exit
            ioc_.stop();
            break;
    }
    ui_->notify();
}

std::string App::get_public_key_hex() const {
    if (!crypto_ || !store_) return "";
    
    // Get the identity public key from crypto engine
    // This is a simplified version - in production, get from crypto_->identity()
    // For now return empty string - would need proper integration with crypto engine
    return "(public key display not yet implemented)";
}

void App::on_theme_changed(const std::string& theme_name) {
    cfg_.ui.theme = theme_name;
    ui::apply_theme(theme_name);
    // Note: In a full implementation, this would update the color scheme
    // and refresh the UI immediately
}

void App::save_current_config() {
    save_config(cfg_, config_path_);
}

} // namespace ircord
