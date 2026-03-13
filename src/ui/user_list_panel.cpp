#include "ui/user_list_panel.hpp"
#include "ui/color_scheme.hpp"
#include <ftxui/dom/elements.hpp>
#include <algorithm>

using namespace ftxui;

namespace ircord::ui {

namespace {

// Voice status indicators
const char* kIndicatorTalking = "\U0001F7E2";  // 🟢 Green circle
const char* kIndicatorMuted   = "\U0001F7E1";  // 🟡 Yellow circle  
const char* kIndicatorOff     = "\u26AA";       // ⚪ White circle

Element render_user_entry(const ChannelUserInfo& user, const std::string& local_user_id) {
    std::string prefix = user.prefix();
    std::string display = prefix + user.user_id;
    
    // Color based on role
    Color name_color;
    if (user.role == UserRole::Admin) {
        name_color = palette::red();
    } else if (user.role == UserRole::Voice) {
        name_color = palette::green();
    } else {
        name_color = palette::fg();
    }
    
    // Highlight local user
    if (user.user_id == local_user_id) {
        name_color = palette::cyan();
    }
    
    return text(display) | color(name_color);
}

Element render_voice_user(const std::string& user_id, ChannelUserInfo::VoiceStatus status,
                          const std::string& local_user_id) {
    const char* indicator = kIndicatorOff;
    Color indicator_color = palette::comment();
    
    switch (status) {
        case ChannelUserInfo::VoiceStatus::Talking:
            indicator = kIndicatorTalking;
            indicator_color = palette::online();  // Green
            break;
        case ChannelUserInfo::VoiceStatus::Muted:
            indicator = kIndicatorMuted;
            indicator_color = palette::yellow();
            break;
        case ChannelUserInfo::VoiceStatus::Off:
        default:
            indicator = kIndicatorOff;
            indicator_color = palette::comment();
            break;
    }
    
    Color name_color = (user_id == local_user_id) ? palette::cyan() : palette::fg();
    
    return hbox({
        text(indicator) | color(indicator_color),
        text(" ") | color(palette::fg()),
        text(user_id) | color(name_color),
    });
}

} // anonymous namespace

Element render_toggle_button(bool collapsed) {
    return text(collapsed ? ">>" : "<<") | color(palette::blue()) | bold;
}

int get_panel_width(const UserListConfig& config, int term_cols) {
    if (config.collapsed) {
        return 0;  // Collapsed - no panel
    }
    // Clamp width to reasonable bounds
    int min_width = 15;
    int max_width = term_cols / 2;  // Max half the terminal
    return std::clamp(config.width, min_width, max_width);
}

std::vector<ChannelUserInfo> sort_users_by_role(std::vector<ChannelUserInfo> users) {
    std::sort(users.begin(), users.end(), [](const ChannelUserInfo& a, const ChannelUserInfo& b) {
        if (a.role != b.role) return static_cast<int>(a.role) > static_cast<int>(b.role);
        return a.user_id < b.user_id;
    });
    return users;
}

VoiceSection build_voice_section(
    const std::vector<ChannelUserInfo>& users,
    const std::vector<std::string>& voice_participants,
    const std::vector<std::string>& speaking_peers,
    const std::vector<std::string>& muted_users) {
    
    VoiceSection section;
    
    for (const auto& user : users) {
        // Check if user is in voice
        bool in_voice = std::find(voice_participants.begin(), voice_participants.end(), 
                                   user.user_id) != voice_participants.end();
        if (!in_voice) continue;
        
        bool is_speaking = std::find(speaking_peers.begin(), speaking_peers.end(),
                                      user.user_id) != speaking_peers.end();
        bool is_muted = std::find(muted_users.begin(), muted_users.end(),
                                   user.user_id) != muted_users.end();
        
        if (is_speaking) {
            section.talking_users.push_back(user.user_id);
        } else if (is_muted) {
            section.muted_users.push_back(user.user_id);
        } else {
            section.connected_users.push_back(user.user_id);
        }
    }
    
    return section;
}

Element render_user_list_panel(
    const std::vector<ChannelUserInfo>& users,
    const VoiceSection& voice_section,
    const UserListConfig& config,
    const std::string& local_user_id,
    std::vector<std::pair<std::string, int>>& out_user_positions,
    int& out_panel_divider_x,
    int base_y) {
    
    out_user_positions.clear();
    int current_y = base_y + 1;  // +1 for border
    
    if (config.collapsed) {
        // When collapsed, we still need to track the divider position
        out_panel_divider_x = -1;  // No divider when collapsed
        return text("");  // Empty when collapsed
    }
    
    Elements panel_content;
    
    // ── USERS: header ──────────────────────────────────────────────────────
    std::string users_header = "USERS: " + std::to_string(users.size());
    panel_content.push_back(text(users_header) | bold | color(palette::fg_dark()));
    panel_content.push_back(separator() | color(palette::bg_highlight()));
    current_y += 2;  // Header + separator
    
    // ── User list ──────────────────────────────────────────────────────────
    for (const auto& user : users) {
        out_user_positions.push_back({user.user_id, current_y});
        panel_content.push_back(render_user_entry(user, local_user_id));
        current_y++;
    }
    
    // ── VOICE: section (if anyone is in voice) ─────────────────────────────
    int voice_count = static_cast<int>(voice_section.talking_users.size() +
                                        voice_section.muted_users.size() +
                                        voice_section.connected_users.size());
    if (voice_count > 0) {
        panel_content.push_back(text(""));  // Spacer
        current_y++;
        std::string voice_header = "VOICE: " + std::to_string(voice_count);
        panel_content.push_back(text(voice_header) | bold | color(palette::fg_dark()));
        panel_content.push_back(separator() | color(palette::bg_highlight()));
        current_y += 2;
        
        // Speaking users first (green)
        for (const auto& user_id : voice_section.talking_users) {
            panel_content.push_back(render_voice_user(user_id, 
                ChannelUserInfo::VoiceStatus::Talking, local_user_id));
            current_y++;
        }
        
        // Connected but not speaking (white circle)
        for (const auto& user_id : voice_section.connected_users) {
            panel_content.push_back(render_voice_user(user_id,
                ChannelUserInfo::VoiceStatus::Off, local_user_id));
            current_y++;
        }
        
        // Muted users (yellow)
        for (const auto& user_id : voice_section.muted_users) {
            panel_content.push_back(render_voice_user(user_id,
                ChannelUserInfo::VoiceStatus::Muted, local_user_id));
            current_y++;
        }
    }
    
    // Calculate panel divider position (left edge of the panel)
    // This is set by the caller based on layout
    out_panel_divider_x = config.width;
    
    // Build the panel with border
    auto panel = vbox(std::move(panel_content));
    
    return panel | border | color(palette::bg_dark());
}

} // namespace ircord::ui
