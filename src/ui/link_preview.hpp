#pragma once
#include <ftxui/dom/elements.hpp>
#include <string>

namespace ircord::ui {

// Inline link preview box rendered below a message.
struct LinkPreviewData {
    std::string url;
    std::string title;
    std::string description;
};

// Returns an ftxui Element for inline link preview display.
ftxui::Element render_link_preview(const LinkPreviewData& data, int max_cols);

} // namespace ircord::ui
