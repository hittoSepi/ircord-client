#include "ui/message_view.hpp"
#include "ui/color_scheme.hpp"
#include <ftxui/dom/elements.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>

using namespace ftxui;

namespace ircord::ui {

static std::string format_ts(int64_t ms, const std::string& fmt) {
    if (ms == 0) return "[--:--]";
    time_t secs = static_cast<time_t>(ms / 1000);
    struct tm tm_info{};
#ifdef _WIN32
    localtime_s(&tm_info, &secs);
#else
    localtime_r(&secs, &tm_info);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), fmt.c_str(), &tm_info);
    return std::string("[") + buf + "]";
}

static Element render_one(const Message& msg, const std::string& ts_fmt) {
    auto ts_el = text(format_ts(msg.timestamp_ms, ts_fmt) + " ")
                 | color(palette::comment());

    if (msg.type == Message::Type::System || msg.type == Message::Type::VoiceEvent) {
        return hbox({
            ts_el,
            text("* " + msg.content) | color(palette::yellow()),
        });
    }

    return hbox({
        ts_el,
        text("<" + msg.sender_id + "> ") | ftxui::color(nick_color(msg.sender_id)),
        text(msg.content) | color(palette::fg()),
    });
}

Element render_messages(const ChannelState& state,
                         const std::string& timestamp_format,
                         int visible_rows) {
    if (state.messages.empty()) {
        return text("(no messages)") | color(palette::comment()) | center;
    }

    int total      = static_cast<int>(state.messages.size());
    int bottom_idx = total - 1 - state.scroll_offset;
    bottom_idx     = std::clamp(bottom_idx, 0, total - 1);
    int top_idx    = std::max(0, bottom_idx - visible_rows + 1);

    Elements lines;
    for (int i = top_idx; i <= bottom_idx; ++i) {
        lines.push_back(render_one(state.messages[i], timestamp_format));
    }

    return vbox(std::move(lines));
}

} // namespace ircord::ui
