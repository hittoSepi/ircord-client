#include "ui/status_bar.hpp"
#include "ui/color_scheme.hpp"
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace ircord::ui {

Element render_status_bar(const StatusInfo& info) {
    // Left side
    Elements left;
    if (info.connected) {
        left.push_back(text("\u25CF ") | color(palette::online()));
    } else {
        left.push_back(text("\u25CB ") | color(palette::error_c()));
    }

    if (!info.local_user_id.empty()) {
        left.push_back(text(info.local_user_id) | color(palette::fg_dark()));
    }
    if (!info.active_channel.empty()) {
        left.push_back(text(" | " + info.active_channel) | color(palette::fg_dark()));
    }
    if (info.in_voice) {
        left.push_back(text(" | \U0001F3A4") | color(palette::blue()));
        if (info.muted) left.push_back(text(" [MUTED]") | color(palette::red()));
        for (auto& p : info.voice_participants) {
            left.push_back(text(" " + p) | color(palette::cyan()));
        }
    }

    // Right side: online users
    Elements right;
    for (auto& u : info.online_users) {
        right.push_back(text("\u25CF " + u + " ") | color(palette::online()));
    }

    return hbox({
        hbox(std::move(left)),
        filler(),
        hbox(std::move(right)),
    }) | bgcolor(palette::bg_dark());
}

} // namespace ircord::ui
