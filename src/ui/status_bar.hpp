#pragma once
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace ircord::ui {

struct StatusInfo {
    bool        connected      = false;
    std::string local_user_id;
    std::string active_channel;
    bool        in_voice       = false;
    bool        muted          = false;
    bool        deafened       = false;
    std::string voice_channel;
    std::string voice_mode     = "ptt";  // "ptt" or "vox"
    std::vector<std::string> voice_participants;
    std::vector<std::string> speaking_peers;  // subset of participants currently speaking
    std::vector<std::string> online_users;
};

ftxui::Element render_status_bar(const StatusInfo& info);

} // namespace ircord::ui
