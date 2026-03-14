#include "ui/input_line.hpp"
#include <algorithm>
#include <stdexcept>

namespace ircord::ui {

InputLine::InputLine(int max_history)
    : max_history_(max_history) {}

void InputLine::insert(char32_t ch) {
    buf_.insert(cursor_, 1, ch);
    ++cursor_;
    hist_pos_ = -1; // editing breaks history browse
}

void InputLine::insert_text(const std::string& utf8) {
    auto decoded = from_utf8(utf8);
    if (decoded.empty()) return;

    buf_.insert(buf_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                decoded.begin(), decoded.end());
    cursor_ += decoded.size();
    hist_pos_ = -1;
}

void InputLine::backspace() {
    if (cursor_ == 0) return;
    buf_.erase(cursor_ - 1, 1);
    --cursor_;
}

void InputLine::del_forward() {
    if (cursor_ >= buf_.size()) return;
    buf_.erase(cursor_, 1);
}

void InputLine::move_left() {
    if (cursor_ > 0) --cursor_;
}

void InputLine::move_right() {
    if (cursor_ < buf_.size()) ++cursor_;
}

void InputLine::move_home() { cursor_ = 0; }
void InputLine::move_end()  { cursor_ = buf_.size(); }

void InputLine::history_prev() {
    if (history_.empty()) return;
    if (hist_pos_ == -1) {
        saved_input_ = buf_;
        hist_pos_    = 0;
    } else if (hist_pos_ < static_cast<int>(history_.size()) - 1) {
        ++hist_pos_;
    } else {
        return;
    }
    buf_    = history_[hist_pos_];
    cursor_ = buf_.size();
}

void InputLine::history_next() {
    if (hist_pos_ == -1) return;
    if (hist_pos_ == 0) {
        hist_pos_ = -1;
        buf_      = saved_input_;
    } else {
        --hist_pos_;
        buf_ = history_[hist_pos_];
    }
    cursor_ = buf_.size();
}

std::string InputLine::commit() {
    std::string result = to_utf8(buf_);
    if (!buf_.empty()) {
        history_.push_front(buf_);
        if (static_cast<int>(history_.size()) > max_history_) history_.pop_back();
    }
    buf_.clear();
    cursor_  = 0;
    hist_pos_ = -1;
    return result;
}

void InputLine::clear() {
    buf_.clear();
    cursor_  = 0;
    hist_pos_ = -1;
}

void InputLine::set_text(const std::string& utf8) {
    buf_ = from_utf8(utf8);
    cursor_   = buf_.size();
    hist_pos_ = -1;
}

std::string InputLine::text() const { return to_utf8(buf_); }
int         InputLine::cursor_col() const { return static_cast<int>(cursor_); }

int InputLine::cursor_byte_offset() const {
    // Convert code point position to UTF-8 byte offset
    auto prefix = buf_.substr(0, cursor_);
    return static_cast<int>(to_utf8(prefix).size());
}

std::u32string InputLine::from_utf8(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0;
        size_t bytes = 0;

        if (c < 0x80) {
            cp = c;
            bytes = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = c & 0x1F;
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = c & 0x0F;
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = c & 0x07;
            bytes = 4;
        } else {
            ++i;
            continue;
        }

        bool valid = true;
        for (size_t b = 1; b < bytes; ++b) {
            unsigned char next = static_cast<unsigned char>(s[i + b]);
            if ((next & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3F);
        }
        if (!valid) {
            ++i;
            continue;
        }

        i += bytes;

        if (cp == U'\r' || cp == U'\n') continue;
        if (cp == U'\t') cp = U' ';
        if (cp < 32 || cp == 127) continue;

        out.push_back(cp);
    }

    return out;
}

std::string InputLine::to_utf8(const std::u32string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (char32_t cp : s) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

} // namespace ircord::ui
