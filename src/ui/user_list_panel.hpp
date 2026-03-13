#pragma once

#include "state/app_state.hpp"
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace ircord::ui {

// Configuration for the user list panel (persisted)
struct UserListConfig {
    int  width        = 20;      // Default width in columns
    bool collapsed    = false;   // Start expanded
    bool show_offline = false;   // Show offline users
};

// Render data for a user entry
struct UserListEntry {
    std::string user_id;
    std::string prefix;      // "@", "+", or ""
    ChannelUserInfo::VoiceStatus voice_status = ChannelUserInfo::VoiceStatus::Off;
    bool is_local_user = false;
};

// Voice section info
struct VoiceSection {
    std::vector<std::string> talking_users;    // Currently speaking
    std::vector<std::string> muted_users;      // Muted but in voice
    std::vector<std::string> connected_users;  // In voice but silent
};

// Build the user list panel element
// Returns a pair: (panel_element, toggle_button_element)
// The toggle button should be placed in the header row
ftxui::Element render_user_list_panel(
    const std::vector<ChannelUserInfo>& users,
    const VoiceSection& voice_section,
    const UserListConfig& config,
    const std::string& local_user_id,
    std::vector<std::pair<std::string, int>>& out_user_positions,  // user_id -> y position
    int& out_panel_divider_x,  // x position of the panel divider (for resizing)
    int base_y = 1);  // Starting y position (after tab bar)

// Simple version without position tracking (backward compatible)
inline ftxui::Element render_user_list_panel(
    const std::vector<ChannelUserInfo>& users,
    const VoiceSection& voice_section,
    const UserListConfig& config,
    const std::string& local_user_id) {
    std::vector<std::pair<std::string, int>> dummy;
    int dummy_x;
    return render_user_list_panel(users, voice_section, config, local_user_id, dummy, dummy_x);
}

// Render just the toggle button (<< or >>)
ftxui::Element render_toggle_button(bool collapsed);

// Get the width of the toggle button for hit testing
inline int get_toggle_button_width() { return 2; }  // "<<" or ">>"

// Calculate the effective width of the panel based on config
int get_panel_width(const UserListConfig& config, int term_cols);

// Sort users by role then alphabetically
std::vector<ChannelUserInfo> sort_users_by_role(std::vector<ChannelUserInfo> users);

// Group users by voice status for the VOICE: section
VoiceSection build_voice_section(
    const std::vector<ChannelUserInfo>& users,
    const std::vector<std::string>& voice_participants,
    const std::vector<std::string>& speaking_peers,
    const std::vector<std::string>& muted_users);

} // namespace ircord::ui
