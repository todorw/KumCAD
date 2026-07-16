#pragma once

#include "core/document/Document.h"

#include <TopoDS_Shape.hxx>

#include <vector>

namespace lcad {

// A sheet-metal part as a strip of constant width/thickness, described by
// its NEUTRAL-AXIS path: a sequence of flat runs separated by bends. This
// is a deliberate, disclosed simplification -- real sheet-metal tools
// (SolidWorks Sheet Metal, FreeCAD's SheetMetal workbench) let you flange
// off an arbitrary selected edge of an arbitrary solid; this models the
// common case directly (a single strip folded into an L/U/Z/hat-channel
// bracket) rather than needing face/edge selection in the still-unverified
// 3D viewport (the same kind of scope cut Sprint 3's Fillet/Chamfer made).
//
// flatLengths.size() must equal bendAngles.size() + 1 (N flats, N-1 bends
// between them). Bend angles are in degrees; positive turns the strip's
// heading left (counter-clockwise) in its own local profile plane.
struct SheetMetalPart {
    double width = 10.0;
    double thickness = 1.0;
    double bendRadius = 1.0; // inside bend radius, shared by every bend
    double kFactor = 0.44;   // standard sheet-metal bend-allowance k-factor
    std::vector<double> flatLengths;
    std::vector<double> bendAngles;
};

// Builds the real 3D solid: each bend is modeled as an actual circular arc
// of radius (bendRadius + kFactor*thickness) around the neutral axis, not
// a sharp corner rounded off afterward -- so the solid's own neutral-axis
// path length exactly matches flatPatternLength()'s formula, by
// construction, rather than by approximation. Returns a null shape if the
// part is geometrically invalid (mismatched array sizes, non-positive
// dimensions, a bend whose inner offset radius would be non-positive, or
// any |bendAngle| >= 180 -- that last one a disclosed scope limit, since
// the neutral-path arc-trimming math assumes less than a half-turn).
TopoDS_Shape buildSheetMetalSolid(const SheetMetalPart& part);

// The exact flat-pattern length along the strip's neutral axis: every flat
// run plus each bend's allowance (BA = angle_rad * (bendRadius +
// kFactor*thickness), the standard sheet-metal formula). Returns 0.0 for
// an invalid part (see buildSheetMetalSolid).
double flatPatternLength(const SheetMetalPart& part);

// Bakes the flat pattern into doc2d as a closed rectangle
// (flatPatternLength(part) x width) on a "FLATPATTERN" layer, with a
// dashed (LineType::Center) bend line across the strip at each bend's
// position along that unfolded length -- matching how real fabrication
// drawings mark where to bend flat stock.
void insertFlatPatternIntoDocument(Document& doc2d, const SheetMetalPart& part, double offsetX, double offsetY);

} // namespace lcad
