#pragma once
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace ircord::ui {

ftxui::Element render_tab_bar(const std::vector<std::string>& channels,
                               const std::string& active_channel,
                               const std::vector<int>& unread_counts);

} // namespace ircord::ui
