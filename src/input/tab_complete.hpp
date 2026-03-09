#pragma once
#include <optional>
#include <string>
#include <vector>

namespace ircord {

class TabCompleter {
public:
    // Attempt to complete the partial string given available candidates.
    // Call reset() when user types a non-Tab key to restart cycling.
    // Returns the completed string, or the original if no match.
    std::string complete(const std::string& partial,
                         const std::vector<std::string>& online_users,
                         const std::vector<std::string>& channels,
                         const std::vector<std::string>& commands);

    void reset();

private:
    bool                     in_cycle_  = false;
    std::string              stem_;             // original partial
    std::vector<std::string> candidates_;
    int                      cycle_pos_ = 0;

    std::vector<std::string> build_candidates(
        const std::string& partial,
        const std::vector<std::string>& online_users,
        const std::vector<std::string>& channels,
        const std::vector<std::string>& commands);
};

} // namespace ircord
