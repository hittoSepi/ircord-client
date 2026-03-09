#pragma once
#include "db/local_store.hpp"

// libsignal-protocol-c C headers
#include <signal_protocol.h>

#include <memory>
#include <string>

namespace ircord::crypto {

// Adapts LocalStore to the 5 signal_protocol_*_store interfaces required
// by libsignal-protocol-c.
class SignalStore {
public:
    explicit SignalStore(db::LocalStore& store, const std::string& local_user_id);
    ~SignalStore();

    // Register all 5 store callbacks on an externally-owned store context.
    void register_with_context(signal_protocol_store_context* store_ctx);

private:
    db::LocalStore&  local_store_;
    std::string      local_user_id_;

    signal_protocol_session_store        session_store_{};
    signal_protocol_pre_key_store        pre_key_store_{};
    signal_protocol_signed_pre_key_store spk_store_{};
    signal_protocol_identity_key_store   id_store_{};
    signal_protocol_sender_key_store     sk_store_{};

    // C-callback implementations (static, use user_data = SignalStore*)
    static int  session_load(signal_buffer** record, signal_buffer** user_record,
                    const signal_protocol_address* addr, void* ud);
    static int  session_get_sub_device_sessions(signal_int_list** sessions,
                    const char* name, size_t name_len, void* ud);
    static int  session_store_cb(const signal_protocol_address* addr,
                    uint8_t* record, size_t record_len,
                    uint8_t* user_record, size_t user_record_len, void* ud);
    static int  session_contains(const signal_protocol_address* addr, void* ud);
    static int  session_delete(const signal_protocol_address* addr, void* ud);
    static int  session_delete_all(const char* name, size_t name_len, void* ud);
    static void session_destroy(void* ud);

    static int  pre_key_load(signal_buffer** record, uint32_t pre_key_id, void* ud);
    static int  pre_key_store_cb(uint32_t pre_key_id, uint8_t* record, size_t len, void* ud);
    static int  pre_key_remove(uint32_t pre_key_id, void* ud);
    static int  pre_key_contains(uint32_t pre_key_id, void* ud);
    static void pre_key_destroy(void* ud);

    static int  spk_load(signal_buffer** record, uint32_t spk_id, void* ud);
    static int  spk_store_cb(uint32_t spk_id, uint8_t* record, size_t len, void* ud);
    static int  spk_remove(uint32_t spk_id, void* ud);
    static int  spk_contains(uint32_t spk_id, void* ud);
    static void spk_destroy(void* ud);

    static int  id_get_key_pair(signal_buffer** public_data, signal_buffer** private_data, void* ud);
    static int  id_get_local_registration(void* ud, uint32_t* registration_id);
    static int  id_save_identity(const signal_protocol_address* addr,
                    uint8_t* key_data, size_t key_len, void* ud);
    static int  id_is_trusted(const signal_protocol_address* addr,
                    uint8_t* key_data, size_t key_len, void* ud);
    static void id_destroy(void* ud);

    static int  sk_store_cb(const signal_protocol_sender_key_name* name,
                    uint8_t* record, size_t len,
                    uint8_t* user_record, size_t user_record_len, void* ud);
    static int  sk_load(signal_buffer** record, signal_buffer** user_record,
                    const signal_protocol_sender_key_name* name, void* ud);
    static void sk_destroy(void* ud);
};

} // namespace ircord::crypto
