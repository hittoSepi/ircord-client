#pragma once
#include <cstdint>

namespace ircord::ui {

struct PlaneGeometry {
    int y, x, rows, cols;
};

struct LayoutDimensions {
    PlaneGeometry tab_bar;     // top strip: channel tabs
    PlaneGeometry msg_view;    // main content: message history
    PlaneGeometry status_bar;  // bottom strip above input
    PlaneGeometry input_line;  // bottom strip: text input
};

// Heights for fixed panels
inline constexpr int kTabBarHeight    = 1;
inline constexpr int kStatusBarHeight = 1;
inline constexpr int kInputLineHeight = 1;

// Compute layout given terminal dimensions.
// Returns nullopt if terminal is too small to render meaningfully.
LayoutDimensions compute_layout(int term_rows, int term_cols);

bool layout_is_valid(int term_rows, int term_cols);

} // namespace ircord::ui
