#include "crypto/group_session.hpp"
#include <group_session_builder.h>
#include <group_cipher.h>
#include <protocol.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace ircord::crypto {

GroupSession::GroupSession(signal_protocol_store_context* store_ctx,
                           signal_context* signal_ctx)
    : store_ctx_(store_ctx), signal_ctx_(signal_ctx) {}

GroupSession::~GroupSession() = default;

std::vector<uint8_t> GroupSession::create_session(const std::string& channel_id) {
    signal_protocol_sender_key_name sender_key_name{};
    sender_key_name.group_id      = channel_id.c_str();
    sender_key_name.group_id_len  = channel_id.size();
    sender_key_name.sender.name   = local_id_.c_str();
    sender_key_name.sender.name_len = local_id_.size();
    sender_key_name.sender.device_id = local_device_id_;

    group_session_builder* builder = nullptr;
    int rc = group_session_builder_create(&builder, store_ctx_, signal_ctx_);
    if (rc != SG_SUCCESS) {
        throw std::runtime_error("group_session_builder_create failed: " + std::to_string(rc));
    }

    sender_key_distribution_message* skdm = nullptr;
    rc = group_session_builder_create_session(builder, &skdm, &sender_key_name);
    group_session_builder_free(builder);
    if (rc != SG_SUCCESS) {
        throw std::runtime_error("group_session_builder_create_session failed: " + std::to_string(rc));
    }

    ciphertext_message* cm = reinterpret_cast<ciphertext_message*>(skdm);
    signal_buffer* serialized = ciphertext_message_get_serialized(cm);
    std::vector<uint8_t> result(signal_buffer_data(serialized),
                                 signal_buffer_data(serialized) + signal_buffer_len(serialized));
    SIGNAL_UNREF(skdm);
    return result;
}

void GroupSession::process_sender_key_distribution(
    const std::string& channel_id,
    const std::string& sender_id,
    int                device_id,
    const std::vector<uint8_t>& distribution_msg)
{
    signal_protocol_sender_key_name sender_key_name{};
    sender_key_name.group_id      = channel_id.c_str();
    sender_key_name.group_id_len  = channel_id.size();
    sender_key_name.sender.name   = sender_id.c_str();
    sender_key_name.sender.name_len = sender_id.size();
    sender_key_name.sender.device_id = device_id;

    sender_key_distribution_message* skdm = nullptr;
    int rc = sender_key_distribution_message_deserialize(
        &skdm, distribution_msg.data(), distribution_msg.size(), signal_ctx_);
    if (rc != SG_SUCCESS) {
        spdlog::error("Failed to deserialize SenderKeyDistributionMessage: {}", rc);
        return;
    }

    group_session_builder* builder = nullptr;
    rc = group_session_builder_create(&builder, store_ctx_, signal_ctx_);
    if (rc == SG_SUCCESS) {
        group_session_builder_process_session(builder, &sender_key_name, skdm);
        group_session_builder_free(builder);
    }
    SIGNAL_UNREF(skdm);
}

std::vector<uint8_t> GroupSession::encrypt(const std::string& channel_id,
                                             const std::vector<uint8_t>& plaintext) {
    signal_protocol_sender_key_name sender_key_name{};
    sender_key_name.group_id      = channel_id.c_str();
    sender_key_name.group_id_len  = channel_id.size();
    sender_key_name.sender.name   = local_id_.c_str();
    sender_key_name.sender.name_len = local_id_.size();
    sender_key_name.sender.device_id = local_device_id_;

    group_cipher* cipher = nullptr;
    int rc = group_cipher_create(&cipher, store_ctx_, &sender_key_name, signal_ctx_);
    if (rc != SG_SUCCESS) throw std::runtime_error("group_cipher_create failed");

    ciphertext_message* encrypted = nullptr;
    rc = group_cipher_encrypt(cipher, plaintext.data(), plaintext.size(), &encrypted);
    group_cipher_free(cipher);
    if (rc != SG_SUCCESS) throw std::runtime_error("group_cipher_encrypt failed");

    signal_buffer* buf = ciphertext_message_get_serialized(encrypted);
    std::vector<uint8_t> result(signal_buffer_data(buf),
                                 signal_buffer_data(buf) + signal_buffer_len(buf));
    SIGNAL_UNREF(encrypted);
    return result;
}

std::vector<uint8_t> GroupSession::decrypt(const std::string& channel_id,
                                             const std::string& sender_id,
                                             int device_id,
                                             const std::vector<uint8_t>& ciphertext) {
    signal_protocol_sender_key_name sender_key_name{};
    sender_key_name.group_id      = channel_id.c_str();
    sender_key_name.group_id_len  = channel_id.size();
    sender_key_name.sender.name   = sender_id.c_str();
    sender_key_name.sender.name_len = sender_id.size();
    sender_key_name.sender.device_id = device_id;

    group_cipher* cipher = nullptr;
    int rc = group_cipher_create(&cipher, store_ctx_, &sender_key_name, signal_ctx_);
    if (rc != SG_SUCCESS) throw std::runtime_error("group_cipher_create failed");

    signal_buffer* plaintext_buf = nullptr;
    sender_key_message* skm = nullptr;
    rc = sender_key_message_deserialize(&skm, ciphertext.data(), ciphertext.size(), signal_ctx_);
    if (rc == SG_SUCCESS) {
        rc = group_cipher_decrypt(cipher, skm, nullptr, &plaintext_buf);
        SIGNAL_UNREF(skm);
    }
    group_cipher_free(cipher);

    if (rc != SG_SUCCESS || !plaintext_buf) {
        throw std::runtime_error("group_cipher_decrypt failed: " + std::to_string(rc));
    }

    std::vector<uint8_t> result(signal_buffer_data(plaintext_buf),
                                 signal_buffer_data(plaintext_buf) + signal_buffer_len(plaintext_buf));
    signal_buffer_free(plaintext_buf);
    return result;
}

} // namespace ircord::crypto
