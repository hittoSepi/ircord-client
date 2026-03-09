#include "ui/link_preview.hpp"
#include "ui/color_scheme.hpp"
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace ircord::ui {

ftxui::Element render_link_preview(const LinkPreviewData& data, int max_cols) {
    // One-line preview: "  ┌ title — description"
    std::string line = "  \u250C " + data.title;
    if (!data.description.empty()) {
        line += " \u2014 " + data.description;
    }
    if (max_cols > 0 && static_cast<int>(line.size()) > max_cols) {
        line.resize(static_cast<size_t>(max_cols - 1));
        line += "\u2026";  // …
    }
    return text(line) | ftxui::color(palette::blue1()) | bgcolor(palette::bg_dark());
}

} // namespace ircord::ui
