#pragma once
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>
#include <utility>

namespace ircord::ui {

// Render tab bar and return positions for mouse hit testing
// Returns vector of (channel_name, x_position) pairs for each tab
ftxui::Element render_tab_bar(const std::vector<std::string>& channels,
                               const std::string& active_channel,
                               const std::vector<int>& unread_counts,
                               std::vector<std::pair<std::string, int>>& out_positions);

// Simple version without position tracking (backward compatible)
inline ftxui::Element render_tab_bar(const std::vector<std::string>& channels,
                                      const std::string& active_channel,
                                      const std::vector<int>& unread_counts) {
    std::vector<std::pair<std::string, int>> dummy;
    return render_tab_bar(channels, active_channel, unread_counts, dummy);
}

} // namespace ircord::ui
