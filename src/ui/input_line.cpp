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

std::string InputLine::text() const { return to_utf8(buf_); }
int         InputLine::cursor_col() const { return static_cast<int>(cursor_); }

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
