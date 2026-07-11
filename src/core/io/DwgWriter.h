#pragma once

#include "core/document/Document.h"

#include <string>

namespace lcad {

// True when this build carries LibreDWG-backed DWG export.
bool dwgWriteSupportAvailable();

// Writes the document's model space as an AutoCAD R2000 DWG via LibreDWG's
// (experimental) add API. Covers lines, circles, arcs, straight polylines,
// text, mtext, ellipses, splines-with-fit-points, blocks/inserts, and
// linear/aligned/radial/diameter/angular dimensions; bulged polyline
// segments are exploded to arcs, and hatches/leaders/paper space are skipped
// (skippedOut reports how many). DXF remains the lossless format.
bool writeDwg(const Document& document, const std::string& path, std::string* errorOut = nullptr,
              int* skippedOut = nullptr);

} // namespace lcad
