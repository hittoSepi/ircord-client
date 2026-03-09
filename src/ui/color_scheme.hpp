#pragma once
#include <ftxui/screen/color.hpp>
#include <cstdint>
#include <string_view>

namespace ircord::ui {

// Tokyo Night palette as FTXUI colors
namespace palette {
    inline ftxui::Color bg()           { return ftxui::Color::RGB(0x1a,0x1b,0x26); }
    inline ftxui::Color bg_dark()      { return ftxui::Color::RGB(0x16,0x16,0x1e); }
    inline ftxui::Color bg_highlight() { return ftxui::Color::RGB(0x29,0x2e,0x42); }
    inline ftxui::Color fg()           { return ftxui::Color::RGB(0xc0,0xca,0xf5); }
    inline ftxui::Color fg_dark()      { return ftxui::Color::RGB(0xa9,0xb1,0xd6); }
    inline ftxui::Color comment()      { return ftxui::Color::RGB(0x56,0x5f,0x89); }
    inline ftxui::Color blue()         { return ftxui::Color::RGB(0x7a,0xa2,0xf7); }
    inline ftxui::Color blue1()        { return ftxui::Color::RGB(0x2a,0xc3,0xde); }
    inline ftxui::Color cyan()         { return ftxui::Color::RGB(0x73,0xda,0xca); }
    inline ftxui::Color magenta()      { return ftxui::Color::RGB(0xbb,0x9a,0xf7); }
    inline ftxui::Color orange()       { return ftxui::Color::RGB(0xff,0x9e,0x64); }
    inline ftxui::Color yellow()       { return ftxui::Color::RGB(0xe0,0xaf,0x68); }
    inline ftxui::Color green()        { return ftxui::Color::RGB(0x9e,0xce,0x6a); }
    inline ftxui::Color red()          { return ftxui::Color::RGB(0xf7,0x76,0x8e); }
    inline ftxui::Color purple()       { return ftxui::Color::RGB(0x9d,0x7c,0xd8); }
    inline ftxui::Color teal()         { return ftxui::Color::RGB(0x1a,0xbc,0x9c); }
    inline ftxui::Color online()       { return green(); }
    inline ftxui::Color offline_c()    { return comment(); }
    inline ftxui::Color unread_badge() { return red(); }
    inline ftxui::Color error_c()      { return red(); }
} // namespace palette

// Nick pool for hash-based coloring
inline ftxui::Color nick_color(std::string_view nick) {
    static const ftxui::Color pool[] = {
        palette::blue(), palette::green(), palette::yellow(), palette::orange(),
        palette::magenta(), palette::cyan(), palette::red(), palette::blue1(),
        palette::purple(), palette::teal(),
    };
    static constexpr int N = static_cast<int>(std::size(pool));

    uint32_t hash = 2166136261u;
    for (unsigned char c : nick) { hash ^= c; hash *= 16777619u; }
    return pool[hash % N];
}

} // namespace ircord::ui
