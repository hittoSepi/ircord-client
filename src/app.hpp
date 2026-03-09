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
#include "input/tab_complete.hpp"
#include "input/command_parser.hpp"

#include <boost/asio/io_context.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace ircord {

class App {
public:
    App();
    ~App();

    bool init(const std::filesystem::path& config_path,
              const std::string& user_id_override = {});

    // Runs FTXUI event loop on main thread (blocks until quit).
    int run();

private:
    // Called by UIManager when user presses Enter
    void on_submit(const std::string& line);
    void handle_command(const ParsedCommand& cmd);
    void send_chat(const std::string& text);
    void switch_channel(int delta);
    void switch_to_channel(const std::string& channel_id);

    ClientConfig cfg_;

    std::unique_ptr<db::LocalStore>         store_;
    AppState                                state_;
    std::unique_ptr<ui::UIManager>          ui_;
    std::unique_ptr<crypto::CryptoEngine>   crypto_;
    std::unique_ptr<voice::VoiceEngine>     voice_;
    std::unique_ptr<LinkPreviewer>          previewer_;

    boost::asio::io_context                 ioc_;
    std::shared_ptr<net::NetClient>         net_client_;
    std::unique_ptr<net::MessageHandler>    msg_handler_;

    std::thread io_thread_;

    TabCompleter tab_completer_;
};

} // namespace ircord
