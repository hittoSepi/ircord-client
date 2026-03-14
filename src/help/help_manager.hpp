#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ircord {

class HelpManager {
public:
    // Construct with path to the directory containing the binary.
    explicit HelpManager(const std::filesystem::path& binary_dir);

    // Scan help/ directory and cache all .md files.
    void load();

    // Clear cache and reload from disk.
    void reload();

    // Return sorted list of available topic names (without .md extension).
    std::vector<std::string> topics() const;

    // Get cached content for a topic. Returns nullopt if not found.
    std::optional<std::string> get(const std::string& topic) const;

private:
    std::filesystem::path help_dir_;
    std::map<std::string, std::string> cache_;
};

} // namespace ircord
