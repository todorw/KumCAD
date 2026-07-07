#pragma once

#include "core/document/Document.h"

#include <string>

namespace lcad {

// Writes an ASCII DXF (R2000-ish, group-code/value pairs) file covering the
// entity subset KumCAD supports: LINE, CIRCLE, ARC, LWPOLYLINE, plus a LAYER
// table. Colors are written as DXF true-color (group 420) for lossless
// round-tripping, with a plain ACI fallback (group 62) for older readers.
// Returns true on success; on failure, *errorOut (if given) gets a message.
bool writeDxf(const Document& document, const std::string& path, std::string* errorOut = nullptr);

} // namespace lcad
