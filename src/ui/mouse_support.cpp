#include "ui/mouse_support.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>
#include <vector>

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

namespace {

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

std::optional<std::string> read_pipe_output(const char* command) {
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return std::nullopt;
    }

    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }

    pclose(pipe);
    if (output.empty()) {
        return std::nullopt;
    }
    return output;
}

#ifdef _WIN32
std::wstring utf8_to_utf16(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    int wide_len = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr, 0);
    if (wide_len <= 0) {
        return {};
    }

    std::wstring wide(static_cast<size_t>(wide_len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        wide.data(), wide_len);
    return wide;
}

std::string utf16_to_utf8(const wchar_t* text) {
    if (!text || *text == L'\0') {
        return {};
    }

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 1) {
        return {};
    }

    std::string utf8(static_cast<size_t>(utf8_len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), utf8_len, nullptr, nullptr);
    utf8.pop_back();
    return utf8;
}
#endif

} // namespace

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
    if (!OpenClipboard(nullptr)) return;

    EmptyClipboard();

    std::wstring wide = utf8_to_utf16(text);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE,
        (wide.size() + 1) * sizeof(wchar_t));
    if (hMem) {
        auto* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
        if (pMem) {
            std::copy(wide.begin(), wide.end(), pMem);
            pMem[wide.size()] = L'\0';
            GlobalUnlock(hMem);
            if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
                GlobalFree(hMem);
            }
        } else {
            GlobalFree(hMem);
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

std::optional<std::string> read_from_clipboard() {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) {
        return std::nullopt;
    }

    std::optional<std::string> result;

    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
            result = utf16_to_utf8(text);
            GlobalUnlock(handle);
        }
    } else if (HANDLE handle = GetClipboardData(CF_TEXT)) {
        if (auto* text = static_cast<const char*>(GlobalLock(handle))) {
            result = std::string(text);
            GlobalUnlock(handle);
        }
    }

    CloseClipboard();
    return result;
#elif __APPLE__
    return read_pipe_output("pbpaste");
#else
    if (auto text = read_pipe_output("wl-paste -n 2>/dev/null")) {
        return text;
    }
    return read_pipe_output("xclip -selection clipboard -o 2>/dev/null");
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
