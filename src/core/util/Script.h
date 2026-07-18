#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lcad {

// AutoCAD command script (.scr) parsing. A script is command-line input
// played back verbatim: whitespace-separated tokens act like Enter-
// terminated entries, a blank line is a bare Enter, and a line whose first
// non-space character is ';' is a comment (skipped entirely -- it is not
// an Enter). Token offsets into the raw line are kept so the player can
// hand a command in a free-text stage (e.g. TEXT's "Enter text:") the
// untokenized remainder of the line, spaces included -- the same reason
// "TEXT 0,0 2.5 0 Hello world" works in real AutoCAD.
//
// Disclosed simplifications vs. real .scr: trailing blanks at the end of a
// line do not count as extra Enters. DELAY and RSCRIPT (both real script-
// control tokens) are recognized and handled by CommandDispatcher's own
// script player (runScriptFile), not by parseScript itself -- this parser
// only tokenizes, it doesn't know what a token means. RESUME isn't
// implemented: real AutoCAD's RESUME continues a script paused by a
// command error, but this player never pauses on error in the first place
// (it plays every line through regardless of individual command
// failures), so there's no paused state for RESUME to meaningfully
// continue.
struct ScriptLine {
    std::string raw;                   // the line, verbatim (comments excluded by parseScript)
    std::vector<std::string> tokens;   // whitespace-separated entries
    std::vector<std::size_t> offsets;  // tokens[i] starts at raw[offsets[i]]
    bool blank() const { return tokens.empty(); }
};

std::vector<ScriptLine> parseScript(const std::string& text);

} // namespace lcad
