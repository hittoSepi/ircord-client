#pragma once

#include "state/channel_state.hpp"
#include "state/voice_state.hpp"

#include <algorithm>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ircord {

enum class PresenceStatus { Offline, Online, Away };

class AppState {
public:
    // ── UI queue (for cross-thread UI updates) ───────────────────────────
    // Post a lambda to be executed on the main (render) thread.
    void post_ui(std::function<void()> fn);

    // Drain and execute all queued UI lambdas. Called from main thread only.
    void drain_ui_queue();

    // ── Channels ─────────────────────────────────────────────────────────
    void ensure_channel(const std::string& channel_id);
    void push_message(const std::string& channel_id, Message msg);
    std::optional<std::string> active_channel() const;
    void set_active_channel(const std::string& channel_id);
    std::vector<std::string> channel_list() const;
    ChannelState channel_snapshot(const std::string& channel_id) const;

    // Scroll
    void scroll_up(const std::string& channel_id, int lines);
    void scroll_down(const std::string& channel_id, int lines);
    void scroll_to_bottom(const std::string& channel_id);

    // Unread
    void mark_read(const std::string& channel_id);
    int  unread_count(const std::string& channel_id) const;

    // ── Presence ─────────────────────────────────────────────────────────
    void set_presence(const std::string& user_id, PresenceStatus status);
    PresenceStatus presence(const std::string& user_id) const;
    std::vector<std::string> online_users() const;

    // ── Voice ─────────────────────────────────────────────────────────────
    VoiceState voice_snapshot() const;
    void set_voice_state(VoiceState vs);

    // ── Connection state ─────────────────────────────────────────────────
    bool connected() const;
    void set_connected(bool v);
    std::string local_user_id() const;
    void set_local_user_id(const std::string& id);

private:
    mutable std::shared_mutex mu_;

    std::unordered_map<std::string, ChannelState> channels_;
    std::string active_channel_;

    std::unordered_map<std::string, PresenceStatus> online_users_;

    VoiceState voice_state_;

    bool        connected_     = false;
    std::string local_user_id_;

    // UI queue
    std::mutex                       ui_queue_mu_;
    std::vector<std::function<void()>> ui_queue_;

    static constexpr int kMaxMessages = 1000;
};

} // namespace ircord
