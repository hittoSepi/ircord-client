#pragma once

#include "state/app_state.hpp"
#include "ui/input_line.hpp"
#include "config.hpp"

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <functional>
#include <string>

namespace ircord::ui {

// Callback type for a submitted input line
using SubmitFn = std::function<void(const std::string&)>;

// UIManager owns the FTXUI ScreenInteractive and builds the full UI component.
// Call run() from the main thread — it blocks until the user quits.
class UIManager {
public:
    UIManager(AppState& state, const ClientConfig& cfg);

    // Build the component tree and run the FTXUI event loop (blocks).
    void run(SubmitFn on_submit, std::function<void()> on_quit);

    // Push a system message to the active (or server) channel.
    // Thread-safe: safe to call from IO/preview threads.
    void push_system_msg(const std::string& text);

    // Wake the FTXUI event loop after AppState changes.
    // Called internally by AppState::post_ui() wiring.
    void notify();

    InputLine& input_line() { return input_line_; }

private:
    ftxui::Element build_document(int term_rows);

    AppState&           state_;
    const ClientConfig& cfg_;
    InputLine           input_line_;

    ftxui::ScreenInteractive screen_{ftxui::ScreenInteractive::Fullscreen()};
};

} // namespace ircord::ui
