#include "input/command_parser.hpp"
#include <sstream>

namespace ircord {

static const std::vector<std::string> kKnownCommands = {
    "/join", "/part", "/msg", "/me",
    "/call", "/hangup", "/accept",
    "/voice", "/mute", "/deafen",
    "/trust", "/safety",
    "/set", "/clear",
    "/quit", "/help",
    "/names", "/whois",
    "/upload", "/download",
};

const std::vector<std::string>& known_commands() {
    return kKnownCommands;
}

std::optional<ParsedCommand> parse_command(std::string_view line) {
    if (line.empty() || line[0] != '/') return std::nullopt;

    // Tokenize
    std::string str(line.substr(1));  // strip leading '/'
    std::istringstream ss(str);
    std::string token;
    std::vector<std::string> tokens;
    while (ss >> token) tokens.push_back(token);

    if (tokens.empty()) return std::nullopt;

    ParsedCommand cmd;
    cmd.name = "/" + tokens[0];
    // Lowercase the command name
    for (char& c : cmd.name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    return cmd;
}

} // namespace ircord
