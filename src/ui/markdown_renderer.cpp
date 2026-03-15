#include "ui/markdown_renderer.hpp"
#include "ui/color_scheme.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/flexbox_config.hpp>
#include <sstream>
#include <vector>
#include <string>

using namespace ftxui;

namespace ircord::ui {

namespace {

// Split a string into per-word Elements for flexbox wrapping.
// Each word and each space becomes a separate Element so flexbox can wrap.
void push_words(Elements& out, const std::string& s, Decorator style) {
    std::string word;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == ' ') {
            if (!word.empty()) {
                out.push_back(text(word) | style);
                word.clear();
            }
            out.push_back(text(" ") | style);
        } else {
            word += s[i];
        }
    }
    if (!word.empty()) {
        out.push_back(text(word) | style);
    }
}

// Parse inline markdown elements within a single line.
// Returns a flexbox of styled word-level segments that wraps properly.
Element parse_inline(const std::string& line) {
    Elements parts;
    size_t i = 0;
    std::string buf;

    auto flush_buf = [&]() {
        if (!buf.empty()) {
            push_words(parts, buf, color(palette::fg()));
            buf.clear();
        }
    };

    while (i < line.size()) {
        // Bold: **text**
        if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*') {
            flush_buf();
            size_t end = line.find("**", i + 2);
            if (end != std::string::npos) {
                push_words(parts, line.substr(i + 2, end - i - 2),
                           bold | color(Color::White));
                i = end + 2;
                continue;
            }
        }
        // Italic: *text* (but not **)
        if (line[i] == '*' && (i + 1 >= line.size() || line[i + 1] != '*')) {
            flush_buf();
            size_t end = line.find('*', i + 1);
            if (end != std::string::npos) {
                push_words(parts, line.substr(i + 1, end - i - 1),
                           italic | color(palette::fg_dark()));
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
                                | color(palette::cyan())
                                | bgcolor(palette::bg_highlight()));
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
    return flexbox(std::move(parts),
                   FlexboxConfig().Set(FlexboxConfig::Wrap::Wrap));
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
                blocks.push_back(vbox(std::move(code_lines))
                                 | color(palette::cyan())
                                 | bgcolor(palette::bg_highlight()));
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
                             | color(palette::blue()));
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
        blocks.push_back(vbox(std::move(code_lines))
                         | color(palette::cyan())
                         | bgcolor(palette::bg_highlight()));
    }

    if (blocks.empty()) return text("");
    return vbox(std::move(blocks));
}

} // namespace ircord::ui
