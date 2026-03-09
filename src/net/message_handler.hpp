#pragma once

#include "state/app_state.hpp"
#include "config.hpp"
#include "ircord.pb.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations
namespace ircord::crypto { class CryptoEngine; }
namespace ircord::voice  { class VoiceEngine;  }
namespace ircord::net    { class NetClient;     }

namespace ircord::net {

// Routes inbound Envelope messages to the appropriate handler.
class MessageHandler {
public:
    MessageHandler(AppState& state,
                   crypto::CryptoEngine& crypto,
                   const ClientConfig& cfg);

    // Called from IO thread for each received Envelope.
    // Must be thread-safe (posts UI updates via app_state_.post_ui).
    void dispatch(const Envelope& env);

    // Inject back-reference for sending (e.g., PONG, KEY_REQUEST)
    void set_net_client(NetClient* nc) { net_client_ = nc; }

    // Inject voice engine for voice signal dispatching
    void set_voice_engine(voice::VoiceEngine* ve) { voice_engine_ = ve; }

    // Called after auth OK: send KEY_UPLOAD
    void on_auth_ok();

    // Track auth state for UI feedback
    bool is_authenticated() const { return authenticated_; }

private:
    void handle_auth_challenge(const Envelope& env);
    void handle_auth_ok(const Envelope& env);
    void handle_auth_fail(const Envelope& env);
    void handle_chat(const Envelope& env);
    void handle_key_bundle(const Envelope& env);
    void handle_presence(const Envelope& env);
    void handle_voice_signal(const Envelope& env);
    void handle_ping(const Envelope& env);
    void handle_error(const Envelope& env);

    // Helpers
    void send_envelope(MessageType type, const google::protobuf::Message& msg);
    void send_hello();
    void push_system(const std::string& text);

    AppState&             state_;
    crypto::CryptoEngine& crypto_;
    const ClientConfig&   cfg_;

    NetClient*           net_client_   = nullptr;
    voice::VoiceEngine*  voice_engine_ = nullptr;

    bool   authenticated_ = false;
    uint64_t next_seq_    = 1;

    // Pending KEY_REQUEST recipients (to flush when KEY_BUNDLE arrives)
    std::unordered_map<std::string, std::string> pending_sends_;
};

} // namespace ircord::net
