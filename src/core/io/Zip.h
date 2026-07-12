#pragma once

#include <string>
#include <utility>
#include <vector>

namespace lcad {

// A minimal ZIP writer (store method only, no compression) with no external
// dependency -- eTransmit's transmittal package doesn't need compression,
// just a single file real CAD users can unzip. entries are (archive name,
// file content) pairs; archive names should already be unique (the caller's
// job, e.g. de-duplicating same-named dependencies from different folders).
// Returns false if the output file can't be written.
bool writeZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& entries);

} // namespace lcad
