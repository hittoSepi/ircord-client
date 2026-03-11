#pragma once

#include "state/app_state.hpp"
#include "ui/input_line.hpp"
#include "input/tab_complete.hpp"
#include "input/command_parser.hpp"
#include "config.hpp"

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>
#include <string>

namespace ircord::ui {

// Callback type for a submitted input line
using SubmitFn = std::function<void(const std::string&)>;
using PttToggleFn = std::function<void()>;

// UIManager owns the FTXUI ScreenInteractive and builds the full UI component.
// Call run() from the main thread — it blocks until the user quits.
class UIManager {
public:
    UIManager(AppState& state, const ClientConfig& cfg);

    // Build the component tree and run the FTXUI event loop (blocks).
    // on_channel_switch(i): called with 0-based index when user presses Alt+1..9.
    void run(SubmitFn on_submit,
             std::function<void()> on_quit,
             std::function<void(int)> on_channel_switch = {},
             PttToggleFn on_ptt_toggle = {});

    // Push a system message to the active (or server) channel.
    // Thread-safe: safe to call from IO/preview threads.
    void push_system_msg(const std::string& text);

    // Wake the FTXUI event loop after AppState changes.
    // Called internally by AppState::post_ui() wiring.
    void notify();

    InputLine& input_line() { return input_line_; }

    // Set PTT active state for hold-to-talk (call with false on key release)
    void set_ptt_active(bool active) { ptt_active_ = active; }

private:
    ftxui::Element build_document(int term_rows);

    AppState&           state_;
    const ClientConfig& cfg_;
    InputLine           input_line_;
    TabCompleter        tab_completer_;

    ftxui::ScreenInteractive screen_{ftxui::ScreenInteractive::Fullscreen()};

    bool ptt_toggled_ = false;  // F1 toggle state for PTT
    bool ptt_active_  = false;  // hold-to-talk state
};

} // namespace ircord::ui
