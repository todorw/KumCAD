#include "core/document/Spreadsheet.h"

#include "core/util/Expr.h"

#include <algorithm>
#include <cstdlib>

namespace lcad {

namespace {

// Parses content as a plain numeric literal (no leading '=') -- the
// whole trimmed string must be consumed, so "12abc" isn't silently
// accepted as 12.
std::optional<double> parseLiteralNumber(const std::string& content) {
    if (content.empty()) return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(content.c_str(), &end);
    if (end != content.c_str() + content.size()) return std::nullopt; // trailing garbage
    return value;
}

} // namespace

void Spreadsheet::setCell(const std::string& cell, std::string content) {
    if (content.empty()) {
        clearCell(cell);
        return;
    }
    m_cells[cell] = std::move(content);
}

void Spreadsheet::clearCell(const std::string& cell) { m_cells.erase(cell); }

bool Spreadsheet::hasCell(const std::string& cell) const { return m_cells.count(cell) > 0; }

const std::string& Spreadsheet::rawContent(const std::string& cell) const {
    static const std::string kEmpty;
    const auto it = m_cells.find(cell);
    return it != m_cells.end() ? it->second : kEmpty;
}

std::vector<std::string> Spreadsheet::cellNames() const {
    std::vector<std::string> names;
    names.reserve(m_cells.size());
    for (const auto& [name, content] : m_cells) {
        (void)content;
        names.push_back(name);
    }
    return names;
}

std::optional<double> Spreadsheet::value(const std::string& cell, std::string* errorOut) const {
    std::vector<std::string> visiting;
    return evaluateCell(cell, visiting, errorOut);
}

std::optional<double> Spreadsheet::evaluateCell(const std::string& cell, std::vector<std::string>& visiting,
                                                std::string* errorOut) const {
    const auto it = m_cells.find(cell);
    if (it == m_cells.end()) {
        if (errorOut) *errorOut = "empty cell";
        return std::nullopt;
    }
    const std::string& content = it->second;

    if (content.empty() || content[0] != '=') {
        const auto literal = parseLiteralNumber(content);
        if (!literal && errorOut) *errorOut = "not a number";
        return literal;
    }

    if (std::find(visiting.begin(), visiting.end(), cell) != visiting.end()) {
        if (errorOut) *errorOut = "circular cell reference";
        return std::nullopt;
    }
    visiting.push_back(cell);

    const std::string formula = content.substr(1);
    // Capture a NESTED cell's own error message separately: passing
    // errorOut straight through would let a real, specific reason (e.g.
    // "circular cell reference") get silently overwritten afterward by
    // evaluateExpression's own generic "unknown identifier" once it sees
    // the lookup simply failed with no explanation.
    std::string nestedError;
    const VariableLookup lookup = [&](const std::string& name) -> std::optional<double> {
        return evaluateCell(name, visiting, &nestedError);
    };
    const auto result = evaluateExpression(formula, lookup, errorOut);
    if (!result && !nestedError.empty() && errorOut) *errorOut = nestedError;

    visiting.pop_back();
    return result;
}

} // namespace lcad
