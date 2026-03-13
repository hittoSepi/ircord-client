#include "state/app_state.hpp"
#include <chrono>
#include <algorithm>

namespace ircord {

// ── UI queue ──────────────────────────────────────────────────────────────────

void AppState::post_ui(std::function<void()> fn) {
    std::lock_guard lk(ui_queue_mu_);
    ui_queue_.push_back(std::move(fn));
}

void AppState::drain_ui_queue() {
    std::vector<std::function<void()>> batch;
    {
        std::lock_guard lk(ui_queue_mu_);
        batch.swap(ui_queue_);
    }
    for (auto& fn : batch) fn();
}

// ── Channels ──────────────────────────────────────────────────────────────────

void AppState::ensure_channel(const std::string& channel_id) {
    std::unique_lock lk(mu_);
    channels_.try_emplace(channel_id);
    if (active_channel_.empty()) active_channel_ = channel_id;
}

void AppState::push_message(const std::string& channel_id, Message msg) {
    std::unique_lock lk(mu_);
    auto& ch = channels_[channel_id];
    ch.messages.push_back(std::move(msg));
    if (static_cast<int>(ch.messages.size()) > kMaxMessages) {
        ch.messages.pop_front();
    }
    // Only increment unread if this isn't the active channel or user has scrolled up
    if (channel_id != active_channel_ || ch.scroll_offset > 0) {
        ++ch.unread_count;
    }
}

std::optional<std::string> AppState::active_channel() const {
    std::shared_lock lk(mu_);
    if (active_channel_.empty()) return std::nullopt;
    return active_channel_;
}

void AppState::set_active_channel(const std::string& channel_id) {
    std::unique_lock lk(mu_);
    active_channel_ = channel_id;
    if (auto it = channels_.find(channel_id); it != channels_.end()) {
        it->second.unread_count = 0;
    }
}

std::vector<std::string> AppState::channel_list() const {
    std::shared_lock lk(mu_);
    std::vector<std::string> result;
    result.reserve(channels_.size());
    for (auto& [id, _] : channels_) result.push_back(id);
    std::sort(result.begin(), result.end());
    return result;
}

ChannelState AppState::channel_snapshot(const std::string& channel_id) const {
    std::shared_lock lk(mu_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return {};
    return it->second;
}

void AppState::remove_channel(const std::string& channel_id) {
    std::unique_lock lk(mu_);
    channels_.erase(channel_id);
    if (active_channel_ == channel_id) {
        // Fall back to "server" if present, else first available, else empty
        if (channels_.count("server"))          active_channel_ = "server";
        else if (!channels_.empty())            active_channel_ = channels_.begin()->first;
        else                                    active_channel_.clear();
    }
}

void AppState::scroll_up(const std::string& channel_id, int lines) {
    std::unique_lock lk(mu_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return;
    it->second.scroll_offset += lines;
}

void AppState::scroll_down(const std::string& channel_id, int lines) {
    std::unique_lock lk(mu_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return;
    it->second.scroll_offset = std::max(0, it->second.scroll_offset - lines);
}

void AppState::scroll_to_bottom(const std::string& channel_id) {
    std::unique_lock lk(mu_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return;
    it->second.scroll_offset = 0;
}

void AppState::mark_read(const std::string& channel_id) {
    std::unique_lock lk(mu_);
    auto it = channels_.find(channel_id);
    if (it != channels_.end()) it->second.unread_count = 0;
}

int AppState::unread_count(const std::string& channel_id) const {
    std::shared_lock lk(mu_);
    auto it = channels_.find(channel_id);
    return it != channels_.end() ? it->second.unread_count : 0;
}

// ── Presence ──────────────────────────────────────────────────────────────────

void AppState::set_presence(const std::string& user_id, PresenceStatus status) {
    std::unique_lock lk(mu_);
    online_users_[user_id] = status;
}

PresenceStatus AppState::presence(const std::string& user_id) const {
    std::shared_lock lk(mu_);
    auto it = online_users_.find(user_id);
    return it != online_users_.end() ? it->second : PresenceStatus::Offline;
}

std::vector<std::string> AppState::online_users() const {
    std::shared_lock lk(mu_);
    std::vector<std::string> result;
    for (auto& [id, status] : online_users_) {
        if (status != PresenceStatus::Offline) result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ── Channel User List ─────────────────────────────────────────────────────────

void AppState::set_channel_users(const std::string& channel_id,
                                  const std::vector<ChannelUserInfo>& users) {
    std::unique_lock lk(mu_);
    auto& ch_users = channel_users_[channel_id];
    ch_users.clear();
    for (const auto& u : users) {
        ChannelUserInfo info = u;
        // Sync with current presence and voice status
        auto pres_it = online_users_.find(u.user_id);
        if (pres_it != online_users_.end()) {
            info.presence = pres_it->second;
        }
        auto voice_it = user_voice_status_.find(u.user_id);
        if (voice_it != user_voice_status_.end()) {
            info.voice_status = voice_it->second;
        }
        ch_users[u.user_id] = std::move(info);
    }
}

void AppState::add_channel_user(const std::string& channel_id, const ChannelUserInfo& user) {
    std::unique_lock lk(mu_);
    ChannelUserInfo info = user;
    // Sync with current presence
    auto pres_it = online_users_.find(user.user_id);
    if (pres_it != online_users_.end()) {
        info.presence = pres_it->second;
    }
    // Sync with voice status
    auto voice_it = user_voice_status_.find(user.user_id);
    if (voice_it != user_voice_status_.end()) {
        info.voice_status = voice_it->second;
    }
    channel_users_[channel_id][user.user_id] = std::move(info);
}

void AppState::remove_channel_user(const std::string& channel_id, const std::string& user_id) {
    std::unique_lock lk(mu_);
    auto ch_it = channel_users_.find(channel_id);
    if (ch_it != channel_users_.end()) {
        ch_it->second.erase(user_id);
    }
}

void AppState::set_user_role(const std::string& channel_id, const std::string& user_id, UserRole role) {
    std::unique_lock lk(mu_);
    auto ch_it = channel_users_.find(channel_id);
    if (ch_it != channel_users_.end()) {
        auto user_it = ch_it->second.find(user_id);
        if (user_it != ch_it->second.end()) {
            user_it->second.role = role;
        }
    }
}

std::vector<ChannelUserInfo> AppState::channel_users(const std::string& channel_id) const {
    std::shared_lock lk(mu_);
    std::vector<ChannelUserInfo> result;
    auto ch_it = channel_users_.find(channel_id);
    if (ch_it != channel_users_.end()) {
        for (const auto& [_, info] : ch_it->second) {
            result.push_back(info);
        }
    }
    // Sort: admins first, then voice, then regular; alphabetically within each group
    std::sort(result.begin(), result.end(), [](const ChannelUserInfo& a, const ChannelUserInfo& b) {
        if (a.role != b.role) return static_cast<int>(a.role) > static_cast<int>(b.role);
        return a.user_id < b.user_id;
    });
    return result;
}

std::optional<ChannelUserInfo> AppState::channel_user(const std::string& channel_id,
                                                const std::string& user_id) const {
    std::shared_lock lk(mu_);
    auto ch_it = channel_users_.find(channel_id);
    if (ch_it != channel_users_.end()) {
        auto user_it = ch_it->second.find(user_id);
        if (user_it != ch_it->second.end()) {
            return user_it->second;
        }
    }
    return std::nullopt;
}

void AppState::ensure_channel_users_from_online(const std::string& channel_id) {
    std::unique_lock lk(mu_);
    // If channel already has users, don't overwrite
    if (channel_users_.count(channel_id)) return;
    
    // Create users from online_users_ with Regular role
    auto& ch_users = channel_users_[channel_id];
    for (const auto& [uid, status] : online_users_) {
        if (status != PresenceStatus::Offline) {
            ChannelUserInfo info;
            info.user_id = uid;
            info.role = UserRole::Regular;
            info.presence = status;
            ch_users[uid] = std::move(info);
        }
    }
}

void AppState::set_user_voice_status(const std::string& user_id, ChannelUserInfo::VoiceStatus status) {
    std::unique_lock lk(mu_);
    user_voice_status_[user_id] = status;
    // Update in all channel user lists
    for (auto& [ch_id, users] : channel_users_) {
        auto it = users.find(user_id);
        if (it != users.end()) {
            it->second.voice_status = status;
        }
    }
}

ChannelUserInfo::VoiceStatus AppState::user_voice_status(const std::string& user_id) const {
    std::shared_lock lk(mu_);
    auto it = user_voice_status_.find(user_id);
    return it != user_voice_status_.end() ? it->second : ChannelUserInfo::VoiceStatus::Off;
}

// ── Voice ─────────────────────────────────────────────────────────────────────

VoiceState AppState::voice_snapshot() const {
    std::shared_lock lk(mu_);
    return voice_state_;
}

void AppState::set_voice_state(VoiceState vs) {
    std::unique_lock lk(mu_);
    voice_state_ = std::move(vs);
}

// ── Connection ────────────────────────────────────────────────────────────────

bool AppState::connected() const {
    std::shared_lock lk(mu_);
    return connected_;
}

void AppState::set_connected(bool v) {
    std::unique_lock lk(mu_);
    connected_ = v;
}

std::string AppState::local_user_id() const {
    std::shared_lock lk(mu_);
    return local_user_id_;
}

void AppState::set_local_user_id(const std::string& id) {
    std::unique_lock lk(mu_);
    local_user_id_ = id;
}

// ── Search results ────────────────────────────────────────────────────────────

void AppState::set_search_results(const std::vector<SearchResult>& results) {
    std::unique_lock lk(mu_);
    search_results_ = results;
}

std::vector<AppState::SearchResult> AppState::search_results() const {
    std::shared_lock lk(mu_);
    return search_results_;
}

void AppState::clear_search_results() {
    std::unique_lock lk(mu_);
    search_results_.clear();
}

} // namespace ircord
