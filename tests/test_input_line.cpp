#include "ui/input_line.hpp"

#include <catch2/catch_test_macros.hpp>

using ircord::ui::InputLine;

TEST_CASE("InputLine inserts pasted ASCII text", "[input_line]") {
    InputLine line;

    line.insert_text("hello world");

    REQUIRE(line.text() == "hello world");
    REQUIRE(line.cursor_col() == 11);
}

TEST_CASE("InputLine preserves UTF-8 code points", "[input_line]") {
    InputLine line;
    const std::string utf8 = "\xC3\xA4\xC3\xB6\xF0\x9F\x99\x82";

    line.insert_text(utf8);

    REQUIRE(line.text() == utf8);
    REQUIRE(line.cursor_col() == 3);
}

TEST_CASE("InputLine strips line breaks and normalizes tabs", "[input_line]") {
    InputLine line;

    line.insert_text("hello\nworld\t!");

    REQUIRE(line.text() == "helloworld !");
}
