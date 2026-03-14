#include "help/help_manager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace ircord {

HelpManager::HelpManager(const std::filesystem::path& binary_dir)
    : help_dir_(binary_dir / "help") {}

void HelpManager::load() {
    cache_.clear();
    if (!std::filesystem::is_directory(help_dir_)) return;

    for (auto& entry : std::filesystem::directory_iterator(help_dir_)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (ext != ".md") continue;

        std::string topic = entry.path().stem().string();
        std::ifstream f(entry.path());
        if (!f) continue;

        std::ostringstream ss;
        ss << f.rdbuf();
        cache_[topic] = ss.str();
    }
}

void HelpManager::reload() {
    load();
}

std::vector<std::string> HelpManager::topics() const {
    std::vector<std::string> result;
    result.reserve(cache_.size());
    for (auto& [k, _] : cache_) {
        result.push_back(k);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<std::string> HelpManager::get(const std::string& topic) const {
    // Sanitize: reject path traversal
    if (topic.find("..") != std::string::npos ||
        topic.find('/') != std::string::npos ||
        topic.find('\\') != std::string::npos) {
        return std::nullopt;
    }

    auto it = cache_.find(topic);
    if (it == cache_.end()) return std::nullopt;
    return it->second;
}

} // namespace ircord
