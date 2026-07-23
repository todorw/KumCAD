#pragma once

#include "core/pcb/Ratsnest.h"

#include <string>
#include <vector>

namespace lcad {

class Document;

// Real .kicad_pcb board format read/write, built on SExpr.h and reusing
// KiCadMod.h's placed-footprint helpers for each footprint instance. A
// real, disclosed subset: general/layers/net table/footprint placements/
// tracks (segment)/vias -- NOT modeled: zones/copper pours, board-level
// graphics (gr_line/gr_arc/gr_text), net classes, and design rules
// beyond a bare-minimum SETUP section. Layer numbering is this codebase's
// own sequential assignment (0, 1, 2, ...) over whichever distinct copper
// layer names actually appear on Track/Via/footprint-side entities in
// the document (F.Cu/B.Cu if none are found), not real KiCad's fixed
// F.Cu=0/B.Cu=31 ordinal convention -- a KumCAD-to-KumCAD round trip is
// exact regardless, but this hasn't been empirically verified against
// real KiCad software (none available to test against here).
//
// Net assignment for tracks/vias is derived by real touch-connectivity:
// a union-find over every track vertex, via position, and each net's own
// resolved pad positions (matching REFDES + pad number against doc's
// placed footprints, same convention Ratsnest.h's own pad resolution
// uses) -- coincident points (within a small tolerance) merge into one
// group, and any track/via in a group that touches a net's pad gets that
// net. A track/via whose group ends up touching more than one net's pads
// (a real short) is assigned whichever net it merged with first, a
// disclosed limitation rather than a DRC check.
bool writeKiCadPcb(const Document& doc, const std::vector<ImportedNet>& nets, const std::string& path,
                   std::string* errorOut = nullptr);

// Reads path back: footprints (via KiCadMod.h's readPlacedFootprintExpr),
// tracks, and vias, resolving each pad/track/via's own (net N ...) tag
// against the file's own net table for the net NAME (not exposed back to
// the caller directly -- Track/Via have no net field of their own, same
// as this codebase's other PCB entities; call parseNetlist/computeRatsnest
// separately if net-aware connectivity is needed afterward).
bool readKiCadPcb(Document& doc, const std::string& path, std::string* errorOut = nullptr);

} // namespace lcad
