#include "core/io/Csv.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace lcad {

std::optional<std::vector<std::vector<std::string>>> readCsv(const std::string& path, std::string* errorOut) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (errorOut) *errorOut = "Could not open file for reading";
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string data = buffer.str();

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool inQuotes = false;
    std::size_t i = 0;

    const auto endField = [&]() {
        row.push_back(field);
        field.clear();
    };
    const auto endRow = [&]() {
        endField();
        rows.push_back(row);
        row.clear();
    };

    while (i < data.size()) {
        const char c = data[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < data.size() && data[i + 1] == '"') {
                    field += '"';
                    i += 2;
                } else {
                    inQuotes = false;
                    ++i;
                }
            } else {
                field += c;
                ++i;
            }
            continue;
        }
        if (c == '"') {
            inQuotes = true;
            ++i;
        } else if (c == ',') {
            endField();
            ++i;
        } else if (c == '\r') {
            ++i; // swallow, \n (or EOF) ends the row
        } else if (c == '\n') {
            endRow();
            ++i;
        } else {
            field += c;
            ++i;
        }
    }
    // Trailing field/row without a final newline.
    if (!field.empty() || !row.empty()) endRow();

    // Drop a single wholly-blank trailing row (common with a trailing newline).
    if (!rows.empty() && rows.back().size() == 1 && rows.back()[0].empty()) rows.pop_back();

    std::size_t width = 0;
    for (const auto& r : rows) width = std::max(width, r.size());
    for (auto& r : rows) r.resize(width);

    return rows;
}

} // namespace lcad
