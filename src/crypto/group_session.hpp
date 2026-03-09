#pragma once
#include <signal_protocol.h>
#include <string>
#include <vector>
#include <cstdint>

namespace ircord::crypto {

// Sender Key group session wrapper (for multi-member channels).
class GroupSession {
public:
    GroupSession(signal_protocol_store_context* store_ctx, signal_context* signal_ctx);
    ~GroupSession();

    void set_local_identity(const std::string& user_id, int device_id = 1) {
        local_id_        = user_id;
        local_device_id_ = device_id;
    }

    // Returns serialized SenderKeyDistributionMessage to send to each member.
    std::vector<uint8_t> create_session(const std::string& channel_id);

    // Process a received SenderKeyDistributionMessage from a member.
    void process_sender_key_distribution(
        const std::string& channel_id,
        const std::string& sender_id,
        int                device_id,
        const std::vector<uint8_t>& distribution_msg);

    // Encrypt a plaintext message for the group channel.
    std::vector<uint8_t> encrypt(const std::string& channel_id,
                                  const std::vector<uint8_t>& plaintext);

    // Decrypt a received group message.
    std::vector<uint8_t> decrypt(const std::string& channel_id,
                                  const std::string& sender_id,
                                  int                device_id,
                                  const std::vector<uint8_t>& ciphertext);

private:
    signal_protocol_store_context* store_ctx_;
    signal_context*                signal_ctx_;
    std::string                    local_id_;
    int                            local_device_id_ = 1;
};

} // namespace ircord::crypto
