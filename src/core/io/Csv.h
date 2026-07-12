#pragma once

#include <optional>
#include <string>
#include <vector>

namespace lcad {

// A minimal RFC-4180-ish CSV reader (comma-separated, "-quoted fields with
// "" as an escaped quote, quoted fields may contain commas/newlines).
// Every row has the same number of fields as the widest row read (short
// rows are padded with empty strings), matching what a TABLE entity needs.
// Returns nullopt with *errorOut set if the file can't be opened.
std::optional<std::vector<std::vector<std::string>>> readCsv(const std::string& path, std::string* errorOut = nullptr);

} // namespace lcad
