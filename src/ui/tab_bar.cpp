#include "ui/tab_bar.hpp"
#include "ui/color_scheme.hpp"
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace ircord::ui {

Element render_tab_bar(const std::vector<std::string>& channels,
                        const std::string& active_channel,
                        const std::vector<int>& unread_counts,
                        std::vector<std::pair<std::string, int>>& out_positions) {
    out_positions.clear();
    Elements tabs;
    int current_x = 0;
    
    for (size_t i = 0; i < channels.size(); ++i) {
        const auto& ch    = channels[i];
        bool        active = (ch == active_channel);
        int         unread = (i < unread_counts.size()) ? unread_counts[i] : 0;

        std::string label = " " + ch;
        if (unread > 0) label += " [" + std::to_string(unread) + "]";
        label += " ";

        // Record position for mouse hit testing
        out_positions.push_back({ch, current_x});

        Element tab = text(label);
        if (active) {
            tab = tab | bold | color(palette::blue()) | bgcolor(palette::bg_highlight());
        } else if (unread > 0) {
            tab = tab | color(palette::unread_badge());
        } else {
            // Add hover effect - use slightly brighter color
            tab = tab | color(palette::fg_dark());
        }

        tabs.push_back(tab);
        current_x += static_cast<int>(label.length());
        
        if (i + 1 < channels.size()) {
            tabs.push_back(text("|") | color(palette::comment()));
            current_x += 1;  // Separator width
        }
    }

    if (tabs.empty()) tabs.push_back(text(" "));
    return hbox(std::move(tabs)) | bgcolor(palette::bg_dark());
}

} // namespace ircord::ui
