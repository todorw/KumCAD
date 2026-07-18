#pragma once

#include "core/document/Document.h"

#include <string>

namespace lcad {

// True when this build carries LibreDWG-backed DWG export.
bool dwgWriteSupportAvailable();

// Writes the document as an AutoCAD R2000 DWG via LibreDWG's (experimental)
// add API. Covers lines, circles, arcs, straight polylines, text, mtext,
// ellipses, splines-with-fit-points, blocks/inserts with attribute
// definitions and values (ATTDEF/ATTRIB), linear/aligned/radial/diameter/
// angular dimensions, leaders (LEADER per MLEADER leg), hatches (real
// pattern/solid fill via a referenced boundary polyline, which itself stays
// in the drawing as visible geometry -- the same outcome AutoCAD leaves with
// "retain boundaries" on), and every layout's paper space (viewports + sheet
// entities; each layout beyond the first gets its own fresh BLOCK_HEADER and
// LAYOUT object). Bulged polyline segments explode to arcs; a TABLE explodes
// into grid lines plus cell text; a WIPEOUT explodes into its own boundary
// as a closed LWPOLYLINE (the masking behavior itself is lost -- a real
// AutoCAD reader sees an empty/wireframe rectangle, not a wipeout).
// skippedOut reports what couldn't be expressed at all: IMAGE (LibreDWG's
// own dwg_add_IMAGE is explicitly documented upstream as "Experimental.
// Does not work yet properly." -- risking a corrupt file isn't worth
// closing this gap) and POINTCLOUD (LibreDWG has no add API for it at
// all). DXF remains the lossless format.
bool writeDwg(const Document& document, const std::string& path, std::string* errorOut = nullptr,
              int* skippedOut = nullptr);

} // namespace lcad
