#pragma once
#include <deque>
#include <string>

namespace ircord::ui {

// Single-line text input buffer with UTF-32 representation, cursor management
// and a persistent command history.
class InputLine {
public:
    explicit InputLine(int max_history = 200);

    // Insert a Unicode code point at the cursor position
    void insert(char32_t ch);
    void insert_text(const std::string& utf8);

    // Delete character before cursor (backspace)
    void backspace();

    // Delete character at cursor (delete key)
    void del_forward();

    // Cursor movement
    void move_left();
    void move_right();
    void move_home();
    void move_end();

    // History navigation (ArrowUp / ArrowDown)
    void history_prev();
    void history_next();

    // Return current buffer as UTF-8 string (for display and send)
    std::string text() const;

    // Return cursor position in display columns (naive: 1 column per char)
    int cursor_col() const;

    // Commit current line to history and clear buffer. Returns the committed line.
    std::string commit();

    // Clear buffer without saving to history
    void clear();

    bool empty() const { return buf_.empty(); }

    // Replace buffer content with a UTF-8 string (used by Tab completion).
    // Cursor moves to end; does not affect history.
    void set_text(const std::string& utf8);

private:
    std::u32string buf_;
    size_t         cursor_ = 0;

    std::deque<std::u32string> history_;
    int                        hist_pos_ = -1;  // -1 = not browsing history
    std::u32string             saved_input_;    // saved while browsing history
    int                        max_history_;

    // Convert u32string to UTF-8
    static std::string to_utf8(const std::u32string& s);
    static std::u32string from_utf8(const std::string& s);
};

} // namespace ircord::ui
