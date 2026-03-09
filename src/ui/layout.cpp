#include "ui/layout.hpp"
#include <algorithm>

namespace ircord::ui {

bool layout_is_valid(int term_rows, int term_cols) {
    return term_rows >= (kTabBarHeight + kStatusBarHeight + kInputLineHeight + 3)
        && term_cols >= 20;
}

LayoutDimensions compute_layout(int term_rows, int term_cols) {
    LayoutDimensions d{};

    // Tab bar — top row
    d.tab_bar = { 0, 0, kTabBarHeight, term_cols };

    // Message view — fills remaining space between tab bar and status bar
    int msg_y    = kTabBarHeight;
    int msg_rows = term_rows - kTabBarHeight - kStatusBarHeight - kInputLineHeight;
    msg_rows     = std::max(1, msg_rows);
    d.msg_view   = { msg_y, 0, msg_rows, term_cols };

    // Status bar — one row above input
    int status_y    = msg_y + msg_rows;
    d.status_bar    = { status_y, 0, kStatusBarHeight, term_cols };

    // Input line — bottom row
    d.input_line = { status_y + kStatusBarHeight, 0, kInputLineHeight, term_cols };

    return d;
}

} // namespace ircord::ui
