#pragma once

#include "state/app_state.hpp"
#include "ui/input_line.hpp"
#include "ui/user_list_panel.hpp"
#include "ui/mouse_support.hpp"
#include "input/tab_complete.hpp"
#include "input/command_parser.hpp"
#include "config.hpp"

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>
#include <string>
#include <optional>
#include <chrono>

namespace ircord::ui {

// Callback type for a submitted input line
using SubmitFn = std::function<void(const std::string&)>;
using PttToggleFn = std::function<void()>;
using OpenSettingsFn = std::function<void()>;
using ChannelCycleFn = std::function<void(int)>;

// UIManager owns the FTXUI ScreenInteractive and builds the full UI component.
// Call run() from the main thread — it blocks until the user quits.
class UIManager {
public:
    UIManager(AppState& state, ClientConfig& cfg);

    // Build the component tree and run the FTXUI event loop (blocks).
    // on_channel_switch(i): called with 0-based index when user presses Alt+1..9.
    // on_channel_cycle(delta): called with -1/+1 for Alt+Left/Alt+Right.
    void run(SubmitFn on_submit,
             std::function<void()> on_quit,
             std::function<void(int)> on_channel_switch = {},
             ChannelCycleFn on_channel_cycle = {},
             PttToggleFn on_ptt_toggle = {},
             OpenSettingsFn on_open_settings = {});

    // Push a system message to the active (or server) channel.
    // Thread-safe: safe to call from IO/preview threads.
    void push_system_msg(const std::string& text);
    void push_system_msg_to_channel(const std::string& channel_id,
                                    const std::string& text,
                                    bool activate_channel = false);

    // Wake the FTXUI event loop after AppState changes.
    // Called internally by AppState::post_ui() wiring.
    void notify();

    // Request the FTXUI event loop to exit (for /quit).
    void request_exit();

    InputLine& input_line() { return input_line_; }

    // Set PTT active state for hold-to-talk (call with false on key release)
    void set_ptt_active(bool active) { ptt_active_ = active; }

    // Toggle user list panel collapsed state
    void toggle_user_list();
    bool is_user_list_collapsed() const { return user_list_config_.collapsed; }
    
    // Mouse handling
    bool handle_mouse_event(const ftxui::Event& event);
    bool is_mouse_on_tab(const std::string& channel, int mouse_x, int mouse_y) const;
    bool is_mouse_on_user_list_toggle(int mouse_x, int mouse_y) const;
    bool is_mouse_on_panel_divider(int mouse_x, int mouse_y) const;
    bool is_mouse_on_user_entry(const std::string& user_id, int mouse_x, int mouse_y) const;
    int get_tab_index_at_position(int mouse_x, int mouse_y) const;
    std::optional<std::string> get_user_at_position(int mouse_x, int mouse_y) const;
    void update_hover_state(int mouse_x, int mouse_y);
    void handle_click(int mouse_x, int mouse_y, bool is_right_click);
    void handle_double_click(int mouse_x, int mouse_y);
    void handle_triple_click(int mouse_x, int mouse_y);
    void start_panel_resize(int mouse_x);
    void update_panel_resize(int mouse_x);
    void end_panel_resize();
    void start_text_selection(int mouse_x, int mouse_y);
    void update_text_selection(int mouse_x, int mouse_y);
    void end_text_selection();
    void copy_selection_to_clipboard();

private:
    ftxui::Element build_document(int term_rows);
    
    // Build the main content area (messages + user list panel)
    ftxui::Element build_main_content(const std::string& active_ch, int msg_rows, int term_cols);

    AppState&           state_;
    ClientConfig&       cfg_;
    InputLine           input_line_;
    TabCompleter        tab_completer_;

    ftxui::ScreenInteractive screen_{ftxui::ScreenInteractive::Fullscreen()};

    bool ptt_toggled_ = false;  // F1 toggle state for PTT
    bool ptt_active_  = false;  // hold-to-talk state
    
    // User list panel state
    UserListConfig user_list_config_;
    
    // Mouse state tracking
    MouseTracker mouse_tracker_;
    
    // Layout info for hit testing (updated during render)
    mutable std::vector<std::pair<std::string, int>> tab_positions_;  // channel -> x position
    mutable std::vector<std::pair<std::string, int>> user_positions_; // user_id -> y position
    mutable int panel_divider_x_ = -1;
    mutable int user_list_y_start_ = 1;  // After header
    
    // Panel resize state
    bool is_resizing_panel_ = false;
    int resize_start_x_ = 0;
    int resize_start_width_ = 0;
};

} // namespace ircord::ui
