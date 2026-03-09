#pragma once
#include <optional>
#include <string>
#include <vector>

namespace ircord {

struct ParsedCommand {
    std::string              name;   // e.g. "join", "msg", "call"
    std::vector<std::string> args;
};

// Parse a line that starts with '/'. Returns nullopt for plain messages.
// Leading '/' is expected to be present.
std::optional<ParsedCommand> parse_command(std::string_view line);

// All known command names (for tab completion)
const std::vector<std::string>& known_commands();

} // namespace ircord
