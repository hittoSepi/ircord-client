#include "ui/mouse_support.hpp"
#include <algorithm>
#include <cctype>
#include <regex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ircord::ui {

// ============================================================================
// MouseTracker Implementation
// ============================================================================

void MouseTracker::record_click(int x, int y) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_click_time_).count();
    
    int dx = std::abs(x - last_click_x_);
    int dy = std::abs(y - last_click_y_);
    
    if (elapsed < MouseConfig::kDoubleClickThresholdMs && 
        dx <= MouseConfig::kClickDistanceThreshold && 
        dy <= MouseConfig::kClickDistanceThreshold) {
        click_count_++;
    } else {
        click_count_ = 1;
    }
    
    last_click_time_ = now;
    last_click_x_ = x;
    last_click_y_ = y;
    x_ = x;
    y_ = y;
}

bool MouseTracker::is_double_click() const {
    return click_count_ == 2;
}

bool MouseTracker::is_triple_click() const {
    return click_count_ >= 3;
}

void MouseTracker::start_drag(int x, int y) {
    is_dragging_ = true;
    drag_start_x_ = x;
    drag_start_y_ = y;
}

void MouseTracker::update_drag(int x, int y) {
    x_ = x;
    y_ = y;
}

void MouseTracker::end_drag() {
    is_dragging_ = false;
}

void MouseTracker::start_selection(int x, int y) {
    is_selecting_ = true;
    selection_start_x_ = x;
    selection_start_y_ = y;
    selection_end_x_ = x;
    selection_end_y_ = y;
}

void MouseTracker::update_selection(int x, int y) {
    selection_end_x_ = x;
    selection_end_y_ = y;
}

void MouseTracker::end_selection() {
    is_selecting_ = false;
}

UIRegion MouseTracker::selection_region() const {
    int x1 = std::min(selection_start_x_, selection_end_x_);
    int y1 = std::min(selection_start_y_, selection_end_y_);
    int x2 = std::max(selection_start_x_, selection_end_x_);
    int y2 = std::max(selection_start_y_, selection_end_y_);
    
    return {x1, y1, x2 - x1 + 1, y2 - y1 + 1};
}

// ============================================================================
// Clipboard Implementation
// ============================================================================

void copy_to_clipboard(const std::string& text) {
#ifdef _WIN32
    // Windows clipboard
    if (!OpenClipboard(nullptr)) return;
    
    EmptyClipboard();
    
    // Allocate global memory for the text
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hMem) {
        char* pMem = static_cast<char*>(GlobalLock(hMem));
        if (pMem) {
            std::copy(text.begin(), text.end(), pMem);
            pMem[text.size()] = '\0';
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
    }
    
    CloseClipboard();
#elif __APPLE__
    // macOS - use pbcopy
    FILE* pipe = popen("pbcopy", "w");
    if (pipe) {
        fwrite(text.c_str(), 1, text.size(), pipe);
        pclose(pipe);
    }
#else
    // Linux - try xclip first, then wl-copy for Wayland
    FILE* pipe = popen("xclip -selection clipboard 2>/dev/null || wl-copy 2>/dev/null", "w");
    if (pipe) {
        fwrite(text.c_str(), 1, text.size(), pipe);
        pclose(pipe);
    }
#endif
}

// ============================================================================
// URL Detection and Opening
// ============================================================================

bool is_url(const std::string& text) {
    static const std::regex url_regex(
        R"((https?://|www\.)[a-zA-Z0-9\-\.]+\.[a-zA-Z]{2,}(/\S*)?)",
        std::regex::icase
    );
    return std::regex_search(text, url_regex);
}

void open_url(const std::string& url) {
    std::string command;
    
#ifdef _WIN32
    // Windows: use start command
    command = "start \"\" \"" + url + "\"";
    std::system(command.c_str());
#elif __APPLE__
    // macOS: use open command
    command = "open \"" + url + "\" 2>/dev/null &";
    std::system(command.c_str());
#else
    // Linux: try xdg-open, then specific browsers
    command = "xdg-open \"" + url + "\" 2>/dev/null || "
              "firefox \"" + url + "\" 2>/dev/null || "
              "chromium \"" + url + "\" 2>/dev/null &";
    std::system(command.c_str());
#endif
}

} // namespace ircord::ui
