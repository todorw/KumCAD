#include <catch2/catch_test_macros.hpp>

#include "core/util/Script.h"

TEST_CASE("parseScript tokenizes lines and treats blank lines as Enter", "[script]") {
    const auto lines = lcad::parseScript("LINE 0,0 10,10 \nCIRCLE 5,5 2\n\nZOOM E\n");
    REQUIRE(lines.size() == 4);

    REQUIRE(lines[0].tokens == std::vector<std::string>{"LINE", "0,0", "10,10"});
    REQUIRE(lines[1].tokens == std::vector<std::string>{"CIRCLE", "5,5", "2"});
    REQUIRE(lines[2].blank()); // the empty line
    REQUIRE(lines[3].tokens == std::vector<std::string>{"ZOOM", "E"});
}

TEST_CASE("parseScript skips comment lines entirely (not even a blank Enter)", "[script]") {
    const auto lines = lcad::parseScript("LINE\n; this is a comment\n  ; indented comment\n0,0\n");
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0].tokens == std::vector<std::string>{"LINE"});
    REQUIRE(lines[1].tokens == std::vector<std::string>{"0,0"});
}

TEST_CASE("parseScript records token offsets into the raw line for free-text tails", "[script]") {
    const auto lines = lcad::parseScript("TEXT 0,0 2.5 0 Hello   world\n");
    REQUIRE(lines.size() == 1);
    const lcad::ScriptLine& line = lines[0];
    REQUIRE(line.tokens.size() == 6);
    REQUIRE(line.tokens[4] == "Hello");
    REQUIRE(line.tokens[5] == "world");
    // The tail from token 4's offset keeps the internal spacing intact.
    REQUIRE(line.raw.substr(line.offsets[4]) == "Hello   world");
}

TEST_CASE("parseScript handles no trailing newline and CRLF line endings", "[script]") {
    const auto noTrailingNewline = lcad::parseScript("ERASE ALL");
    REQUIRE(noTrailingNewline.size() == 1);
    REQUIRE(noTrailingNewline[0].tokens == std::vector<std::string>{"ERASE", "ALL"});

    const auto crlf = lcad::parseScript("LINE\r\n0,0\r\n");
    REQUIRE(crlf.size() == 2);
    REQUIRE(crlf[0].tokens == std::vector<std::string>{"LINE"});
    REQUIRE(crlf[1].tokens == std::vector<std::string>{"0,0"});
}

TEST_CASE("parseScript on empty input yields no lines", "[script]") {
    REQUIRE(lcad::parseScript("").empty());
}
