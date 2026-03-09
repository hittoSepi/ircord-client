#include "ui/tab_bar.hpp"
#include "ui/color_scheme.hpp"
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace ircord::ui {

Element render_tab_bar(const std::vector<std::string>& channels,
                        const std::string& active_channel,
                        const std::vector<int>& unread_counts) {
    Elements tabs;
    for (size_t i = 0; i < channels.size(); ++i) {
        const auto& ch    = channels[i];
        bool        active = (ch == active_channel);
        int         unread = (i < unread_counts.size()) ? unread_counts[i] : 0;

        std::string label = " " + ch;
        if (unread > 0) label += " [" + std::to_string(unread) + "]";
        label += " ";

        Element tab = text(label);
        if (active) {
            tab = tab | bold | color(palette::blue()) | bgcolor(palette::bg_highlight());
        } else if (unread > 0) {
            tab = tab | color(palette::unread_badge());
        } else {
            tab = tab | color(palette::fg_dark());
        }

        tabs.push_back(tab);
        if (i + 1 < channels.size()) {
            tabs.push_back(text("|") | color(palette::comment()));
        }
    }

    if (tabs.empty()) tabs.push_back(text(" "));
    return hbox(std::move(tabs)) | bgcolor(palette::bg_dark());
}

} // namespace ircord::ui
