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

} // namespace ircord
