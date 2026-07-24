#pragma once

#include "core/document/Document.h"

#include <string>

namespace lcad {

// Reads a DXF file into document, replacing its current contents entirely
// (including clearing undo history, since a freshly loaded file shouldn't
// carry over an unrelated edit history). Understands every entity type this
// codebase's own writer produces (see DxfWriter.h) plus the LAYER/STYLE/
// DIMSTYLE tables, blocks with attributes, xrefs, and layouts with
// viewports; anything else in the file (OBJECTS, unsupported entity types,
// ...) is silently skipped rather than causing a failure, since real-world
// DXF files commonly contain sections we don't need to round-trip. Malformed
// input (truncated file, garbage numeric fields, a non-DXF file with a .dxf
// extension) is reported as a failure rather than crashing.
// Returns true on success; on failure, *errorOut (if given) gets a message.
bool readDxf(Document& document, const std::string& path, std::string* errorOut = nullptr);

} // namespace lcad
