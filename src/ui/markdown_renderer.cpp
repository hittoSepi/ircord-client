#include "ui/markdown_renderer.hpp"
#include "ui/color_scheme.hpp"
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <vector>
#include <string>

using namespace ftxui;

namespace ircord::ui {

namespace {

// Parse inline markdown elements within a single line.
// Returns an hbox of styled text segments.
Element parse_inline(const std::string& line) {
    Elements parts;
    size_t i = 0;
    std::string buf;

    auto flush_buf = [&]() {
        if (!buf.empty()) {
            parts.push_back(text(buf) | color(palette::fg()));
            buf.clear();
        }
    };

    while (i < line.size()) {
        // Bold: **text**
        if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*') {
            flush_buf();
            size_t end = line.find("**", i + 2);
            if (end != std::string::npos) {
                parts.push_back(text(line.substr(i + 2, end - i - 2))
                                | bold | color(palette::fg()));
                i = end + 2;
                continue;
            }
        }
        // Italic: *text* (but not **)
        if (line[i] == '*' && (i + 1 >= line.size() || line[i + 1] != '*')) {
            flush_buf();
            size_t end = line.find('*', i + 1);
            if (end != std::string::npos) {
                parts.push_back(text(line.substr(i + 1, end - i - 1))
                                | dim | color(palette::fg()));
                i = end + 1;
                continue;
            }
        }
        // Inline code: `text`
        if (line[i] == '`') {
            flush_buf();
            size_t end = line.find('`', i + 1);
            if (end != std::string::npos) {
                parts.push_back(text(line.substr(i + 1, end - i - 1))
                                | inverted);
                i = end + 1;
                continue;
            }
        }
        buf += line[i];
        ++i;
    }
    flush_buf();

    if (parts.empty()) return text("");
    if (parts.size() == 1) return parts[0];
    return hbox(std::move(parts));
}

} // anonymous namespace

Element render_markdown(const std::string& md) {
    std::istringstream stream(md);
    std::string line;
    Elements blocks;
    bool in_code_block = false;
    Elements code_lines;

    while (std::getline(stream, line)) {
        // Code block fence: ```
        if (line.size() >= 3 && line.substr(0, 3) == "```") {
            if (in_code_block) {
                // End code block
                blocks.push_back(vbox(std::move(code_lines)) | inverted);
                code_lines.clear();
                in_code_block = false;
            } else {
                in_code_block = true;
            }
            continue;
        }

        if (in_code_block) {
            code_lines.push_back(text(line));
            continue;
        }

        // Heading: # text
        if (!line.empty() && line[0] == '#') {
            size_t level = 0;
            while (level < line.size() && line[level] == '#') ++level;
            std::string heading = line.substr(level);
            // Trim leading space
            if (!heading.empty() && heading[0] == ' ') heading = heading.substr(1);
            blocks.push_back(text(heading) | bold | underlined
                             | color(palette::fg()));
            continue;
        }

        // Blockquote: > text
        if (!line.empty() && line[0] == '>') {
            std::string content = line.substr(1);
            if (!content.empty() && content[0] == ' ') content = content.substr(1);
            blocks.push_back(hbox({
                text("│ ") | color(palette::comment()),
                parse_inline(content) | dim,
            }));
            continue;
        }

        // Bullet list: - text or * text (at start of line)
        if (line.size() >= 2 &&
            (line[0] == '-' || line[0] == '*') && line[1] == ' ') {
            std::string content = line.substr(2);
            blocks.push_back(hbox({
                text("  • ") | color(palette::yellow()),
                parse_inline(content),
            }));
            continue;
        }

        // Normal line with inline parsing
        if (line.empty()) {
            blocks.push_back(text(""));
        } else {
            blocks.push_back(parse_inline(line));
        }
    }

    // Unclosed code block
    if (in_code_block && !code_lines.empty()) {
        blocks.push_back(vbox(std::move(code_lines)) | inverted);
    }

    if (blocks.empty()) return text("");
    return vbox(std::move(blocks));
}

} // namespace ircord::ui
