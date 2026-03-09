#pragma once
#include <deque>
#include <optional>
#include <string>
#include <cstdint>

namespace ircord {

struct Message {
    std::string sender_id;
    std::string content;
    int64_t     timestamp_ms = 0;

    enum class Type { Chat, System, VoiceEvent } type = Type::Chat;

    // Pending link preview (filled asynchronously)
    struct Preview {
        std::string title;
        std::string description;
        bool        loaded = false;
    };
    std::optional<Preview> link_preview;
};

struct ChannelState {
    std::deque<Message> messages;    // max 1000, old ones dropped
    int unread_count  = 0;
    int scroll_offset = 0;           // lines scrolled up from bottom
};

} // namespace ircord
