#pragma once

#include "core/geometry/Point2D.h"

#include <optional>
#include <string>
#include <vector>

namespace lcad {

class Document;
struct BlockDefinition;
struct SExpr;

// Writes block as a real KiCad .kicad_mod footprint file (modern
// "footprint" s-expression form, KiCad 6+): name, a smd/through_hole
// attr (derived from whether any pad has a drill), every Pad as a KiCad
// (pad ...), and every Line/Arc/Circle/Text child entity of block as
// fp_line/fp_arc/fp_circle/fp_text respectively. A real, interoperable
// subset -- not modeled: fp_poly graphic items, 3D model references,
// net-tie groups, private (front/back-independent) layers, roundrect/
// trapezoid pad corner geometry (written back as a plain rect), and any
// child entity kind other than the four listed (Polyline/Spline/MText/
// etc. children are silently skipped), the same "real subset, disclosed"
// spirit as SpecctraWriter.h. Pad-level rotation is dropped on write
// (Pad, Block.h, has no rotation field of its own). Coordinate/rotation
// sign conventions are written literally (no Y-flip attempted for
// KiCad's own Y-down board coordinate system) -- this keeps a KumCAD-to-
// KumCAD round trip exact, but this hasn't been empirically verified
// against real KiCad software (none available to test against here), so
// treat orientation against an actual KiCad install as unverified.
bool writeKiCadMod(const Document& doc, const BlockDefinition& block, const std::string& path,
                   std::string* errorOut = nullptr);

// Reads path (a real .kicad_mod file, or this writer's own output) as a
// new BlockDefinition added to doc (renamed with a numeric suffix if the
// file's own footprint name is already taken -- addBlock never
// overwrites). Returns the added block, or nullptr on a read/parse
// failure (*errorOut set). Accepts both the modern "footprint" root tag
// and the legacy pre-6 "module" tag.
const BlockDefinition* readKiCadMod(Document& doc, const std::string& path, std::string* errorOut = nullptr);

// The same footprint body writeKiCadMod produces, but as an embeddable
// (footprint ...) s-expression rather than a standalone file: adds an
// (at x y rot) placement and, for each pad with a net assignment
// (padNetNumbers[i] > 0), a (net N "name") tag -- what a .kicad_pcb's own
// per-instance footprint entries need that a standalone .kicad_mod
// doesn't. backSide tags the footprint's own layer as B.Cu instead of
// F.Cu; padNetNumbers/padNetNames are parallel to block.pads (a net
// number <= 0 or an out-of-range index leaves that pad without a net
// tag). Coordinates/rotation are written literally for a back-side
// footprint too (no mirroring attempted) -- the same disclosed
// simplification writeKiCadMod's own header comment already makes.
SExpr buildPlacedFootprintExpr(const Document& doc, const BlockDefinition& block, Point2D at, double rotationDeg,
                               bool backSide, const std::vector<int>& padNetNumbers,
                               const std::vector<std::string>& padNetNames);

// One footprint instance parsed out of a .kicad_pcb's own (footprint ...)
// entry (as opposed to readKiCadMod's whole-standalone-file parse):
// the new BlockDefinition (added to doc, body/pads populated exactly as
// readKiCadMod would), its board placement, and each pad's own raw net
// NUMBER (parallel to block->pads) -- resolving that number to a net NAME
// is the caller's job, since only the caller (holding the board's own
// net table) can do that.
struct ParsedPlacedFootprint {
    const BlockDefinition* block = nullptr;
    Point2D position;
    double rotationDeg = 0.0;
    bool backSide = false;
    std::vector<int> padNetNumbers;
};
std::optional<ParsedPlacedFootprint> readPlacedFootprintExpr(Document& doc, const SExpr& footprintExpr);

} // namespace lcad
