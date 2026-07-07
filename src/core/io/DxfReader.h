#pragma once

#include "core/document/Document.h"

#include <string>

namespace lcad {

// Reads a DXF file into document, replacing its current contents entirely
// (including clearing undo history, since a freshly loaded file shouldn't
// carry over an unrelated edit history). Only LINE/CIRCLE/ARC/LWPOLYLINE
// entities and the LAYER table are understood; everything else in the file
// (other entity types, BLOCKS, OBJECTS, dimension styles, ...) is silently
// skipped rather than causing a failure, since real-world DXF files commonly
// contain sections we don't need to round-trip.
// Returns true on success; on failure, *errorOut (if given) gets a message.
bool readDxf(Document& document, const std::string& path, std::string* errorOut = nullptr);

} // namespace lcad
