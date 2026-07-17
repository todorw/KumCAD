#include "core/util/Script.h"

namespace lcad {

std::vector<ScriptLine> parseScript(const std::string& text) {
    std::vector<ScriptLine> lines;

    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string raw = text.substr(start, end - start);
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        start = end + 1;
        if (start > text.size() && raw.empty()) break; // no trailing phantom line

        // Comment line: first non-space char is ';'.
        const std::size_t firstNonSpace = raw.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos && raw[firstNonSpace] == ';') continue;

        ScriptLine line;
        line.raw = raw;
        std::size_t pos = 0;
        while (pos < raw.size()) {
            while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\t')) ++pos;
            if (pos >= raw.size()) break;
            const std::size_t tokenStart = pos;
            while (pos < raw.size() && raw[pos] != ' ' && raw[pos] != '\t') ++pos;
            line.tokens.push_back(raw.substr(tokenStart, pos - tokenStart));
            line.offsets.push_back(tokenStart);
        }
        lines.push_back(std::move(line));

        if (start > text.size()) break;
    }
    return lines;
}

} // namespace lcad
