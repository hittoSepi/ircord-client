#include "ui/ui_manager.hpp"
#include "ui/color_scheme.hpp"
#include "ui/tab_bar.hpp"
#include "ui/status_bar.hpp"
#include "ui/message_view.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <chrono>

using namespace ftxui;

namespace ircord::ui {

UIManager::UIManager(AppState& state, const ClientConfig& cfg)
    : state_(state), cfg_(cfg) {}

void UIManager::notify() {
    screen_.PostEvent(Event::Custom);
}

void UIManager::push_system_msg(const std::string& txt) {
    auto ch = state_.active_channel().value_or("server");
    state_.ensure_channel(ch);
    Message msg;
    msg.type         = Message::Type::System;
    msg.content      = txt;
    msg.sender_id    = "system";
    msg.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    state_.post_ui([this, ch, m = std::move(msg)]() mutable {
        state_.push_message(ch, std::move(m));
    });
    notify();
}

Element UIManager::build_document(int term_rows) {
    // Panels
    auto channels  = state_.channel_list();
    auto active_ch = state_.active_channel().value_or("");

    // Tab bar
    std::vector<int> unread;
    for (auto& c : channels) unread.push_back(state_.unread_count(c));
    auto tab_el = render_tab_bar(channels, active_ch, unread);

    // Message area (estimate: term_rows - 3 for tabs + status + input)
    int msg_rows = std::max(1, term_rows - 3);
    Element msg_el;
    if (!active_ch.empty()) {
        auto ch_state = state_.channel_snapshot(active_ch);
        msg_el = render_messages(ch_state, cfg_.ui.timestamp_format, msg_rows);
    } else {
        msg_el = text("") ;
    }

    // Status bar
    StatusInfo si;
    si.connected      = state_.connected();
    si.local_user_id  = state_.local_user_id();
    si.active_channel = active_ch;
    auto vs           = state_.voice_snapshot();
    si.in_voice       = vs.in_voice;
    si.muted          = vs.muted;
    si.deafened       = vs.deafened;
    si.voice_channel  = vs.active_channel;
    si.voice_mode     = vs.voice_mode;
    si.voice_participants = vs.participants;
    si.speaking_peers = vs.speaking_peers;
    si.online_users   = state_.online_users();
    auto status_el = render_status_bar(si);

    // Input line
    std::string display = "> " + input_line_.text();
    // Show block cursor
    int cur = input_line_.cursor_col();
    std::string before = "> " + input_line_.text().substr(0, cur);
    std::string at     = (cur < static_cast<int>(input_line_.text().size()))
                         ? input_line_.text().substr(cur, 1) : " ";
    std::string after  = (cur < static_cast<int>(input_line_.text().size()))
                         ? input_line_.text().substr(cur + 1) : "";

    auto input_el = hbox({
        text(before) | color(palette::fg()),
        text(at)     | color(palette::bg()) | bgcolor(palette::fg()),
        text(after)  | color(palette::fg()),
    }) | bgcolor(palette::bg());

    return vbox({
        tab_el,
        msg_el | flex,
        separator(),
        status_el,
        input_el,
    }) | bgcolor(palette::bg());
}

void UIManager::run(SubmitFn on_submit,
                    std::function<void()> on_quit,
                    std::function<void(int)> on_channel_switch,
                    PttToggleFn on_ptt_toggle) {
    auto renderer = Renderer([&] {
        // Drain cross-thread UI updates before rendering
        state_.drain_ui_queue();

        int rows = screen_.dimx() > 0 ? screen_.dimy() : 24;
        return build_document(rows);
    });

    auto event_handler = CatchEvent(renderer, [&](Event event) -> bool {
        if (event == Event::Custom) {
            // Posted by notify() — just triggers a redraw
            return true;
        }
        // F1 — PTT toggle (since FTXUI doesn't support key-release, use toggle)
        if (event == Event::F1) {
            ptt_toggled_ = !ptt_toggled_;
            if (on_ptt_toggle) on_ptt_toggle();
            return true;
        }
        if (event == Event::Return) {
            tab_completer_.reset();
            std::string line = input_line_.commit();
            if (!line.empty() && on_submit) on_submit(line);
            return true;
        }
        if (event == Event::Backspace) {
            tab_completer_.reset();
            input_line_.backspace();
            return true;
        }
        if (event == Event::Delete) {
            tab_completer_.reset();
            input_line_.del_forward();
            return true;
        }
        if (event == Event::ArrowLeft) {
            input_line_.move_left();
            return true;
        }
        if (event == Event::ArrowRight) {
            input_line_.move_right();
            return true;
        }
        if (event == Event::ArrowUp) {
            input_line_.history_prev();
            return true;
        }
        if (event == Event::ArrowDown) {
            input_line_.history_next();
            return true;
        }
        if (event == Event::Home) {
            input_line_.move_home();
            return true;
        }
        // Ctrl+A = move to beginning of line
        if (event.input() == "\x01") {
            input_line_.move_home();
            return true;
        }
        if (event == Event::End) {
            input_line_.move_end();
            return true;
        }
        if (event == Event::PageUp) {
            if (auto ch = state_.active_channel()) state_.scroll_up(*ch, 10);
            return true;
        }
        if (event == Event::PageDown) {
            if (auto ch = state_.active_channel()) state_.scroll_down(*ch, 10);
            return true;
        }
        if (event == Event::Escape || event.input() == "\x03" || event.input() == "\x04") {
            // Ctrl+C or Ctrl+D → quit
            if (on_quit) on_quit();
            screen_.ExitLoopClosure()();
            return true;
        }
        if (event.is_character()) {
            // Normal printable character
            std::string ch = event.character();
            if (!ch.empty()) {
                // Decode first UTF-8 codepoint
                uint32_t cp = static_cast<unsigned char>(ch[0]);
                if (cp >= 32) {
                    tab_completer_.reset();
                    input_line_.insert(static_cast<char32_t>(cp));
                    return true;
                }
            }
        }
        // Tab completion
        if (event.input() == "\t") {
            auto completed = tab_completer_.complete(
                input_line_.text(),
                state_.online_users(),
                state_.channel_list(),
                known_commands());
            input_line_.set_text(completed);
            return true;
        }
        // Alt+1..9 — switch to channel by 0-based index
        if (event.input().size() == 2 &&
            event.input()[0] == '\x1b' &&
            event.input()[1] >= '1' && event.input()[1] <= '9') {
            if (on_channel_switch)
                on_channel_switch(event.input()[1] - '1');
            return true;
        }
        return false;
    });

    screen_.Loop(event_handler);
}

} // namespace ircord::ui
