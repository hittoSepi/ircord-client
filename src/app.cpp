#include "app.hpp"
#include "input/command_parser.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <sodium.h>
#include <boost/asio/executor_work_guard.hpp>
#include <iostream>
#include <chrono>
#include <regex>
#include <thread>

namespace ircord {

App::App() = default;

App::~App() {
    ioc_.stop();
    if (io_thread_.joinable()) io_thread_.join();
    if (previewer_) previewer_->stop();
}

bool App::init(const std::filesystem::path& config_path,
               const std::string& user_id_override) {
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
    std::filesystem::create_directories(cfg_.config_dir);

    // ── Database ──────────────────────────────────────────────────────────
    try {
        store_ = std::make_unique<db::LocalStore>(cfg_.db_path);
    } catch (const std::exception& e) {
        std::cerr << "DB open failed: " << e.what() << "\n";
        return false;
    }

    // ── Crypto (passphrase before TUI starts) ────────────────────────────
    crypto_ = std::make_unique<crypto::CryptoEngine>();

    std::string passphrase;
    if (!store_->has_identity(cfg_.identity.user_id)) {
        std::cout << "New identity for '" << cfg_.identity.user_id
                  << "'. Enter passphrase: ";
    } else {
        std::cout << "Passphrase for '" << cfg_.identity.user_id << "': ";
    }
    std::getline(std::cin, passphrase);

    if (!crypto_->init(*store_, cfg_, passphrase)) {
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

    voice_->set_send_signal([this](const VoiceSignal& sig) {
        Envelope env;
        env.set_type(MT_VOICE_SIGNAL);
        env.set_payload(sig.SerializeAsString());
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

    ui_->push_system_msg("IrssiCord v0.1.0 — connecting to " +
                          cfg_.server.host + ":" + std::to_string(cfg_.server.port));

    // ── FTXUI event loop (blocks main thread) ─────────────────────────────
    ui_->run(
        [this](const std::string& line) { on_submit(line); },
        [this]()                         { ioc_.stop(); }
    );

    // ── Shutdown ─────────────────────────────────────────────────────────
    ioc_.stop();
    work.reset();
    if (io_thread_.joinable()) io_thread_.join();
    if (previewer_) previewer_->stop();

    return 0;
}

void App::on_submit(const std::string& line) {
    tab_completer_.reset();
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
    if (cmd.name == "/join" && !cmd.args.empty()) {
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
        auto ch = state_.active_channel().value_or("general");
        if (voice_->in_voice()) {
            voice_->leave_room();
            ui_->push_system_msg("Left voice room.");
        } else {
            voice_->join_room(ch);
            ui_->push_system_msg("Joined voice room: " + ch);
        }
    } else if (cmd.name == "/mute") {
        bool m = !state_.voice_snapshot().muted;
        voice_->set_muted(m);
        ui_->push_system_msg(m ? "Muted." : "Unmuted.");
    } else if (cmd.name == "/deafen") {
        bool d = !state_.voice_snapshot().deafened;
        voice_->set_deafened(d);
        ui_->push_system_msg(d ? "Deafened." : "Undeafened.");
    } else if (cmd.name == "/trust" && !cmd.args.empty()) {
        std::string peer = cmd.args[0];
        std::string sn   = crypto_->safety_number(peer, *store_);
        ui_->push_system_msg("Safety number with " + peer + ":");
        ui_->push_system_msg("  " + sn);
        auto existing = store_->load_peer_identity(peer);
        if (existing) store_->save_peer_identity(peer, *existing, "verified");
    } else if (cmd.name == "/names") {
        auto users = state_.online_users();
        std::string list;
        for (auto& u : users) list += u + " ";
        ui_->push_system_msg("Online: " + (list.empty() ? "(none)" : list));
    } else if (cmd.name == "/clear") {
        ui_->push_system_msg("(cleared)");
    } else if (cmd.name == "/help") {
        ui_->push_system_msg(
            "Commands: /join /msg /call /accept /hangup /voice "
            "/mute /deafen /trust /names /clear /quit");
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

    // Encrypt and send
    auto env = crypto_->encrypt(local_id, active, text,
        [this](const std::string& recipient_id) {
            KeyRequest kr;
            kr.set_user_id(recipient_id);
            Envelope req;
            req.set_type(MT_KEY_REQUEST);
            req.set_payload(kr.SerializeAsString());
            net_client_->send(req);
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

void App::switch_channel(int delta) {
    auto channels = state_.channel_list();
    if (channels.empty()) return;
    auto active = state_.active_channel().value_or(channels[0]);
    auto it = std::find(channels.begin(), channels.end(), active);
    int  idx = (it == channels.end()) ? 0 : static_cast<int>(it - channels.begin());
    idx = (idx + delta + static_cast<int>(channels.size())) % static_cast<int>(channels.size());
    switch_to_channel(channels[idx]);
}

void App::switch_to_channel(const std::string& channel_id) {
    state_.ensure_channel(channel_id);
    state_.set_active_channel(channel_id);
    ui_->push_system_msg("Switched to " + channel_id);
}

} // namespace ircord
