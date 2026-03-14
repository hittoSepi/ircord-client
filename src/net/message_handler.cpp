#include "net/message_handler.hpp"
#include "net/net_client.hpp"
#include "crypto/crypto_engine.hpp"
#include "voice/voice_engine.hpp"
#include "version.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>

namespace ircord::net {

MessageHandler::MessageHandler(AppState& state,
                                crypto::CryptoEngine& crypto,
                                const ClientConfig& cfg)
    : state_(state), crypto_(crypto), cfg_(cfg) {}

void MessageHandler::dispatch(const Envelope& env) {
    switch (env.type()) {
    case MT_AUTH_CHALLENGE: handle_auth_challenge(env); break;
    case MT_AUTH_OK:        handle_auth_ok(env);        break;
    case MT_AUTH_FAIL:      handle_auth_fail(env);      break;
    case MT_CHAT_ENVELOPE:  handle_chat(env);           break;
    case MT_KEY_BUNDLE:     handle_key_bundle(env);     break;
    case MT_PRESENCE:       handle_presence(env);       break;
    case MT_VOICE_SIGNAL:      handle_voice_signal(env);      break;
    case MT_VOICE_ROOM_JOIN:   handle_voice_room_join(env);   break;
    case MT_VOICE_ROOM_LEAVE:  handle_voice_room_leave(env);  break;
    case MT_VOICE_ROOM_STATE:  handle_voice_room_state(env);  break;
    case MT_PING:              handle_ping(env);              break;
    case MT_ERROR:          handle_error(env);          break;
    case MT_COMMAND_RESPONSE: handle_command_response(env); break;
    default:
        spdlog::debug("Unhandled message type: {}", static_cast<int>(env.type()));
        break;
    }
}

void MessageHandler::handle_auth_challenge(const Envelope& env) {
    AuthChallenge challenge;
    if (!challenge.ParseFromString(env.payload())) {
        spdlog::error("Failed to parse AuthChallenge");
        return;
    }

    const std::string& nonce_str = challenge.nonce();
    std::vector<uint8_t> nonce(nonce_str.begin(), nonce_str.end());

    spdlog::debug("Received auth challenge, signing...");
    if (trace_fn_) trace_fn_("AUTH_CHALLENGE received");

    auto sig     = crypto_.sign_challenge(nonce, cfg_.identity.user_id);
    auto id_pub  = crypto_.identity_pub();
    auto spk_inf = crypto_.current_spk();

    AuthResponse resp;
    resp.set_user_id(cfg_.identity.user_id);
    resp.set_identity_pub(id_pub.data(), id_pub.size());
    resp.set_signature(sig.data(), sig.size());
    resp.set_signed_prekey(spk_inf.pub.data(), spk_inf.pub.size());
    resp.set_spk_sig(spk_inf.sig.data(), spk_inf.sig.size());

    send_envelope(MT_AUTH_RESPONSE, resp);
    if (trace_fn_) trace_fn_("AUTH_RESPONSE sent");
}

void MessageHandler::handle_auth_ok(const Envelope& env) {
    (void)env;
    authenticated_ = true;
    if (trace_fn_) trace_fn_("AUTH_OK received");
    spdlog::info("Authentication successful");
    push_system("Authenticated as " + cfg_.identity.user_id);
    state_.set_connected(true);
    on_auth_ok();
}

void MessageHandler::on_auth_ok() {
    // Upload pre-keys
    auto ku = crypto_.prepare_key_upload(100);
    send_envelope(MT_KEY_UPLOAD, ku);
    spdlog::debug("Uploaded {} one-time pre-keys", ku.opk_ids_size());
}

void MessageHandler::handle_auth_fail(const Envelope& env) {
    (void)env;
    authenticated_ = false;
    if (trace_fn_) trace_fn_("AUTH_FAIL received");
    spdlog::error("Authentication failed");
    push_system("Authentication failed! Check your identity key.");
    state_.set_connected(false);
}

void MessageHandler::handle_chat(const Envelope& env) {
    ChatEnvelope chat;
    if (!chat.ParseFromString(env.payload())) {
        spdlog::warn("Failed to parse ChatEnvelope");
        return;
    }

    std::string plaintext;
    bool        decrypted = false;

    if (chat.ciphertext_type() == 0) {
        // Unencrypted (should not happen in production)
        plaintext = chat.ciphertext();
        decrypted = true;
    } else {
        auto result = crypto_.decrypt(chat);
        if (result.success) {
            plaintext = result.plaintext;
            decrypted = true;
        } else {
            plaintext = "[decryption failed]";
            decrypted = true;
        }
    }

    if (!decrypted) return;

    std::string channel_id = chat.recipient_id();
    if (channel_id.empty()) channel_id = chat.sender_id();

    state_.ensure_channel(channel_id);

    Message msg;
    msg.sender_id   = chat.sender_id();
    msg.content     = plaintext;
    msg.timestamp_ms = static_cast<int64_t>(env.timestamp_ms());
    if (msg.timestamp_ms == 0) {
        msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    msg.type = Message::Type::Chat;

    // Persist to database if callback is set
    if (persist_fn_) {
        persist_fn_(channel_id, msg);
    }

    state_.post_ui([this, channel_id, m = std::move(msg)]() mutable {
        state_.push_message(channel_id, std::move(m));
    });
}

void MessageHandler::handle_key_bundle(const Envelope& env) {
    spdlog::debug("Received KeyBundle payload: size={}", env.payload().size());
    
    KeyBundle bundle;
    if (!bundle.ParseFromString(env.payload())) {
        spdlog::warn("Failed to parse KeyBundle");
        return;
    }

    std::string recipient_id = bundle.recipient_for();
    spdlog::debug("Parsed KeyBundle: recipient_for='{}'", recipient_id);
    
    // WORKAROUND: If recipient_for is empty, use the pending key request target
    if (recipient_id.empty()) {
        spdlog::warn("KeyBundle missing recipient_for field — using pending request target");
        // Get the target from the pending key request queue
        // For now, use the identity key to determine the sender
        if (bundle.identity_pub().size() >= 32) {
            // We need to look up the user by identity key
            // This is a workaround - the server should send recipient_for
            spdlog::debug("Bundle has identity_pub, attempting to identify sender by key");
        }
        // Try to use the last key request target as fallback
        // This requires tracking pending key requests
        spdlog::error("Cannot determine recipient without recipient_for field — need server fix");
        return;
    }

    // Establish X3DH session with the recipient
    crypto_.on_key_bundle(bundle, recipient_id);
    spdlog::info("Established X3DH session with '{}'", recipient_id);

    // Flush pending plaintext if we queued one while waiting for this bundle
    auto it = pending_sends_.find(recipient_id);
    if (it == pending_sends_.end()) return;

    std::string plaintext = std::move(it->second);
    pending_sends_.erase(it);

    ChatEnvelope chat = crypto_.encrypt(
        cfg_.identity.user_id, recipient_id, plaintext,
        [](const std::string&) { /* session exists now — no re-request needed */ });

    if (!chat.sender_id().empty()) {
        send_envelope(MT_CHAT_ENVELOPE, chat);
        spdlog::debug("Flushed pending DM to '{}'", recipient_id);
    } else {
        spdlog::error("Re-encrypt failed for pending DM to '{}'", recipient_id);
    }
}

void MessageHandler::request_key(const std::string& recipient_id,
                                  const std::string& plaintext) {
    pending_sends_[recipient_id] = plaintext;
    KeyRequest kr;
    kr.set_user_id(recipient_id);
    send_envelope(MT_KEY_REQUEST, kr);
    spdlog::debug("KEY_REQUEST sent for '{}', plaintext queued", recipient_id);
}

void MessageHandler::handle_presence(const Envelope& env) {
    PresenceUpdate update;
    if (!update.ParseFromString(env.payload())) return;

    PresenceStatus status;
    switch (update.status()) {
    case PresenceUpdate::ONLINE:  status = PresenceStatus::Online;  break;
    case PresenceUpdate::AWAY:    status = PresenceStatus::Away;    break;
    default:                       status = PresenceStatus::Offline; break;
    }

    const std::string& uid = update.user_id();
    state_.post_ui([this, uid, status]() {
        state_.set_presence(uid, status);
        std::string text = uid + (status == PresenceStatus::Online ? " is now online" : " went offline");
        push_system(text);
    });
}

void MessageHandler::handle_voice_signal(const Envelope& env) {
    // Forward to VoiceEngine
    VoiceSignal vs;
    if (!vs.ParseFromString(env.payload())) return;

    if (voice_engine_) {
        // voice_engine_->on_voice_signal(vs);
        spdlog::debug("Voice signal received from {}", vs.from_user());
    }
}

void MessageHandler::handle_voice_room_state(const Envelope& env) {
    VoiceRoomState state;
    if (!state.ParseFromArray(env.payload().data(),
            static_cast<int>(env.payload().size()))) return;

    // Tell VoiceEngine to set up peer connections to all participants
    if (voice_engine_) {
        std::vector<std::string> peers;
        for (const auto& p : state.participants()) {
            if (p != cfg_.identity.user_id) {
                peers.push_back(p);
            }
        }
        voice_engine_->on_room_joined(state.channel_id(), peers);
    }

    state_.post_ui([this, state]() {
        VoiceState vs = state_.voice_snapshot();
        vs.in_voice = true;
        vs.active_channel = state.channel_id();
        vs.participants.clear();
        for (const auto& p : state.participants()) {
            vs.participants.push_back(p);
        }
        state_.set_voice_state(vs);
    });
}

void MessageHandler::handle_voice_room_join(const Envelope& env) {
    VoiceRoomJoin join;
    if (!join.ParseFromArray(env.payload().data(),
            static_cast<int>(env.payload().size()))) return;

    // Another user joined our room — create peer connection to them
    if (voice_engine_) {
        voice_engine_->on_peer_joined(join.user_id());
    }

    state_.post_ui([this, join]() {
        VoiceState vs = state_.voice_snapshot();
        vs.participants.push_back(join.user_id());
        state_.set_voice_state(vs);

        Message msg;
        msg.type = Message::Type::System;
        msg.sender_id = "system";
        msg.content = join.user_id() + " joined voice";
        msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        state_.push_message(join.channel_id(), std::move(msg));
    });
}

void MessageHandler::handle_voice_room_leave(const Envelope& env) {
    VoiceRoomLeave leave;
    if (!leave.ParseFromArray(env.payload().data(),
            static_cast<int>(env.payload().size()))) return;

    // Remove peer connection
    if (voice_engine_) {
        voice_engine_->on_peer_left(leave.user_id());
    }

    state_.post_ui([this, leave]() {
        VoiceState vs = state_.voice_snapshot();
        auto& p = vs.participants;
        p.erase(std::remove(p.begin(), p.end(), leave.user_id()), p.end());
        state_.set_voice_state(vs);

        Message msg;
        msg.type = Message::Type::System;
        msg.sender_id = "system";
        msg.content = leave.user_id() + " left voice";
        msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        state_.push_message(leave.channel_id(), std::move(msg));
    });
}

void MessageHandler::handle_ping(const Envelope& env) {
    (void)env;
    Envelope pong;
    pong.set_seq(next_seq_++);
    pong.set_timestamp_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    pong.set_type(MT_PONG);
    if (net_client_) net_client_->send(pong);
}

void MessageHandler::handle_error(const Envelope& env) {
    Error err;
    if (!err.ParseFromString(env.payload())) return;
    spdlog::error("Server error {}: {}", err.code(), err.message());
    push_system("Server error: " + err.message());
}

void MessageHandler::handle_command_response(const Envelope& env) {
    CommandResponse response;
    if (!response.ParseFromString(env.payload())) {
        spdlog::warn("Failed to parse CommandResponse");
        return;
    }

    if (command_response_fn_) {
        command_response_fn_(response);
        return;
    }

    push_system("Command " + response.command() + ": " + response.message());
}

void MessageHandler::send_envelope(MessageType type, const google::protobuf::Message& msg) {
    if (!net_client_) return;

    Envelope env;
    env.set_seq(next_seq_++);
    env.set_timestamp_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    env.set_type(type);
    env.set_payload(msg.SerializeAsString());

    net_client_->send(env);
}

void MessageHandler::push_system(const std::string& text) {
    Message msg;
    msg.type         = Message::Type::System;
    msg.content      = text;
    msg.sender_id    = "system";
    msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    state_.post_ui([this, m = std::move(msg)]() mutable {
        state_.ensure_channel("server");
        state_.push_message("server", std::move(m));
    });
}

void MessageHandler::send_hello() {
    Hello hello;
    hello.set_protocol_version(1);
    hello.set_client_version(std::string("ircord-client/") + std::string(ircord::VERSION));
    send_envelope(MT_HELLO, hello);
}

void MessageHandler::send_command(const std::string& cmd, const std::vector<std::string>& args) {
    if (!net_client_) return;
    
    IrcCommand ic;
    ic.set_command(cmd);
    for (const auto& arg : args) {
        ic.add_args(arg);
    }
    
    send_envelope(MT_COMMAND, ic);
    spdlog::debug("Sent command: {} with {} args", cmd, args.size());
}

} // namespace ircord::net
