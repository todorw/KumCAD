#pragma once

#include "core/document/Document.h"

#include <string>

namespace lcad {

// True when this build carries LibreDWG-backed DWG import.
bool dwgSupportAvailable();

// Reads an AutoCAD DWG file into the document (replacing its contents), via
// LibreDWG. Maps the same entity subset the DXF reader handles; unsupported
// object types are skipped. Without LibreDWG in the build this fails with an
// explanatory error. DWG *writing* is intentionally not offered -- LibreDWG's
// write support is still experimental; save as DXF instead.
bool readDwg(Document& document, const std::string& path, std::string* errorOut = nullptr);

} // namespace lcad
