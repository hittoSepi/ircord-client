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
        std::string voice_text = "\U0001F3A4 " + info.voice_channel +
            " " + std::to_string(info.voice_participants.size()) + " users";
        if (info.muted) voice_text += " [MUTED]";
        if (info.deafened) voice_text += " [DEAF]";
        voice_text += " [" + std::string(info.voice_mode == "ptt" ? "PTT: F1" : "VOX") + "]";

        left.push_back(text(" | ") | color(palette::fg_dark()));
        left.push_back(text(voice_text) | color(Color::Green));

        // Show speaking peers
        if (!info.speaking_peers.empty()) {
            std::string speaking_text = "\U0001F50A ";  // speaker icon
            for (size_t i = 0; i < info.speaking_peers.size(); ++i) {
                if (i > 0) speaking_text += ", ";
                speaking_text += info.speaking_peers[i];
            }
            left.push_back(text(" ") | color(palette::fg_dark()));
            left.push_back(text(speaking_text) | color(palette::cyan()));
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
