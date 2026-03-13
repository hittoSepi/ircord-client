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
#include <unordered_set>
#include <vector>

namespace ircord {

enum class PresenceStatus { Offline, Online, Away };

// User role/prefix for display
enum class UserRole { Regular, Voice, Admin };

// Extended user info for the user list panel
// Note: Renamed from UserInfo to ChannelUserInfo to avoid conflict with protobuf UserInfo message
struct ChannelUserInfo {
    std::string user_id;
    UserRole role = UserRole::Regular;
    PresenceStatus presence = PresenceStatus::Offline;
    
    // Voice status indicator
    enum class VoiceStatus { Off, Muted, Talking };
    VoiceStatus voice_status = VoiceStatus::Off;
    
    // Get display prefix based on role
    std::string prefix() const {
        switch (role) {
            case UserRole::Admin: return "@";
            case UserRole::Voice: return "+";
            default: return "";
        }
    }
    
    // Get display name with prefix
    std::string display_name() const {
        return prefix() + user_id;
    }
};

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
    // Remove a channel from state. If it was active, fall back to "server".
    void remove_channel(const std::string& channel_id);

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

    // ── Channel User List (for user list panel) ──────────────────────────
    // Set users for a specific channel (e.g., from NAMES reply)
    void set_channel_users(const std::string& channel_id, 
                           const std::vector<ChannelUserInfo>& users);
    // Auto-populate channel users from online users (for testing/initial load)
    void ensure_channel_users_from_online(const std::string& channel_id);
    // Add/update a user in a channel
    void add_channel_user(const std::string& channel_id, const ChannelUserInfo& user);
    // Remove a user from a channel
    void remove_channel_user(const std::string& channel_id, const std::string& user_id);
    // Set user role (for admin/voice changes)
    void set_user_role(const std::string& channel_id, const std::string& user_id, UserRole role);
    // Get users for a channel (sorted by role then name)
    std::vector<ChannelUserInfo> channel_users(const std::string& channel_id) const;
    // Get user info for a specific user in a channel
    std::optional<ChannelUserInfo> channel_user(const std::string& channel_id, 
                                          const std::string& user_id) const;
    // Update voice status for a user (shown in user list panel)
    void set_user_voice_status(const std::string& user_id, ChannelUserInfo::VoiceStatus status);
    ChannelUserInfo::VoiceStatus user_voice_status(const std::string& user_id) const;

    // ── Voice ─────────────────────────────────────────────────────────────
    VoiceState voice_snapshot() const;
    void set_voice_state(VoiceState vs);

    // ── Connection state ─────────────────────────────────────────────────
    bool connected() const;
    void set_connected(bool v);
    std::string local_user_id() const;
    void set_local_user_id(const std::string& id);

    // ── Search results ────────────────────────────────────────────────────
    struct SearchResult {
        std::string channel_id;
        std::string sender_id;
        std::string content;
        int64_t     timestamp_ms;
    };
    void set_search_results(const std::vector<SearchResult>& results);
    std::vector<SearchResult> search_results() const;
    void clear_search_results();

private:
    mutable std::shared_mutex mu_;

    std::unordered_map<std::string, ChannelState> channels_;
    std::string active_channel_;

    std::unordered_map<std::string, PresenceStatus> online_users_;

    VoiceState voice_state_;

    // Channel membership: channel_id -> (user_id -> ChannelUserInfo)
    std::unordered_map<std::string, std::unordered_map<std::string, ChannelUserInfo>> channel_users_;
    
    // Global voice status for users (shared across channels)
    std::unordered_map<std::string, ChannelUserInfo::VoiceStatus> user_voice_status_;

    bool        connected_     = false;
    std::string local_user_id_;

    std::vector<SearchResult> search_results_;

    // UI queue
    std::mutex                       ui_queue_mu_;
    std::vector<std::function<void()>> ui_queue_;

    static constexpr int kMaxMessages = 1000;
};

} // namespace ircord
