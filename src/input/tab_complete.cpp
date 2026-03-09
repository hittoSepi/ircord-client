#include "input/tab_complete.hpp"
#include <algorithm>
#include <cctype>

namespace ircord {

void TabCompleter::reset() {
    in_cycle_  = false;
    candidates_.clear();
    cycle_pos_ = 0;
}

std::string TabCompleter::complete(const std::string& partial,
                                   const std::vector<std::string>& online_users,
                                   const std::vector<std::string>& channels,
                                   const std::vector<std::string>& commands) {
    if (!in_cycle_) {
        stem_       = partial;
        candidates_ = build_candidates(partial, online_users, channels, commands);
        cycle_pos_  = 0;
        in_cycle_   = true;
    }

    if (candidates_.empty()) return partial;

    std::string result = candidates_[cycle_pos_];
    cycle_pos_ = (cycle_pos_ + 1) % static_cast<int>(candidates_.size());
    return result;
}

std::vector<std::string> TabCompleter::build_candidates(
    const std::string& partial,
    const std::vector<std::string>& online_users,
    const std::vector<std::string>& channels,
    const std::vector<std::string>& commands)
{
    std::vector<std::string> cands;
    if (partial.empty()) return cands;

    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::string lp = lower(partial);

    if (partial[0] == '/') {
        // Command completion
        for (auto& cmd : commands) {
            if (lower(cmd).find(lp) == 0) cands.push_back(cmd + " ");
        }
    } else if (partial[0] == '#') {
        // Channel completion
        for (auto& ch : channels) {
            if (lower(ch).find(lp) == 0) cands.push_back(ch);
        }
    } else {
        // Nick completion
        for (auto& nick : online_users) {
            if (lower(nick).find(lp) == 0) cands.push_back(nick + ": ");
        }
    }

    std::sort(cands.begin(), cands.end());
    return cands;
}

} // namespace ircord
