#include "net/message_handler.hpp"
#include "net/net_client.hpp"
#include "crypto/crypto_engine.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

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
    case MT_VOICE_SIGNAL:   handle_voice_signal(env);   break;
    case MT_PING:           handle_ping(env);           break;
    case MT_ERROR:          handle_error(env);          break;
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
}

void MessageHandler::handle_auth_ok(const Envelope& env) {
    (void)env;
    authenticated_ = true;
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

    state_.post_ui([this, channel_id, m = std::move(msg)]() mutable {
        state_.push_message(channel_id, std::move(m));
    });
}

void MessageHandler::handle_key_bundle(const Envelope& env) {
    KeyBundle bundle;
    if (!bundle.ParseFromString(env.payload())) {
        spdlog::warn("Failed to parse KeyBundle");
        return;
    }

    // Find who we requested for
    // The key bundle doesn't directly say who it's for — we track pending requests
    // The server sends bundles in response to KEY_REQUEST which had a user_id.
    // For now, we process against pending sends.
    // TODO: wire the recipient_id through the proto or track it externally.
    spdlog::debug("Received KeyBundle");
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
    auto ch = state_.active_channel().value_or("server");
    state_.ensure_channel(ch);
    Message msg;
    msg.type         = Message::Type::System;
    msg.content      = text;
    msg.sender_id    = "system";
    msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    state_.post_ui([this, ch, m = std::move(msg)]() mutable {
        state_.push_message(ch, std::move(m));
    });
}

void MessageHandler::send_hello() {
    Hello hello;
    hello.set_protocol_version(1);
    hello.set_client_version("ircord-client/0.1.0");
    send_envelope(MT_HELLO, hello);
}

} // namespace ircord::net
