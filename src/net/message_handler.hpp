#pragma once

#include "state/app_state.hpp"
#include "state/channel_state.hpp"
#include "config.hpp"
#include "ircord.pb.h"

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Forward declarations
namespace ircord::crypto { class CryptoEngine; }
namespace ircord::voice  { class VoiceEngine;  }
namespace ircord::net    { class NetClient;     }
struct Message;

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

    // Callback for message persistence (optional)
    using PersistMsgFn = std::function<void(const std::string& channel_id, const Message& msg)>;
    void set_persist_callback(PersistMsgFn fn) { persist_fn_ = std::move(fn); }
    using TraceFn = std::function<void(const std::string&)>;
    void set_trace_callback(TraceFn fn) { trace_fn_ = std::move(fn); }
    using CommandResponseFn = std::function<void(const CommandResponse&)>;
    void set_command_response_callback(CommandResponseFn fn) { command_response_fn_ = std::move(fn); }

    // Called after auth OK: send KEY_UPLOAD
    void on_auth_ok();

    // Track auth state for UI feedback
    bool is_authenticated() const { return authenticated_; }
    void on_transport_disconnected() { authenticated_ = false; }

    // Send KEY_REQUEST for a DM recipient and track the pending plaintext
    // so it can be sent once the KEY_BUNDLE arrives.
    void request_key(const std::string& recipient_id, const std::string& plaintext);
    
    // Send IRC command to server
    void send_command(const std::string& cmd, const std::vector<std::string>& args);

private:
    void handle_auth_challenge(const Envelope& env);
    void handle_auth_ok(const Envelope& env);
    void handle_auth_fail(const Envelope& env);
    void handle_chat(const Envelope& env);
    void handle_key_bundle(const Envelope& env);
    void handle_presence(const Envelope& env);
    void handle_voice_signal(const Envelope& env);
    void handle_voice_room_join(const Envelope& env);
    void handle_voice_room_leave(const Envelope& env);
    void handle_voice_room_state(const Envelope& env);
    void handle_ping(const Envelope& env);
    void handle_error(const Envelope& env);
    void handle_command_response(const Envelope& env);

    // Helpers
    void send_envelope(MessageType type, const google::protobuf::Message& msg);
    void send_hello();
    void push_system(const std::string& text);

    AppState&             state_;
    crypto::CryptoEngine& crypto_;
    const ClientConfig&   cfg_;

    NetClient*           net_client_   = nullptr;
    voice::VoiceEngine*  voice_engine_ = nullptr;
    PersistMsgFn         persist_fn_;
    TraceFn              trace_fn_;
    CommandResponseFn    command_response_fn_;

    bool   authenticated_ = false;
    uint64_t next_seq_    = 1;

    // Pending KEY_REQUEST recipients (to flush when KEY_BUNDLE arrives)
    // Stores multiple queued plaintexts per recipient
    std::unordered_map<std::string, std::vector<std::string>> pending_sends_;
};

} // namespace ircord::net
