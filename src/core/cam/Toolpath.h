#pragma once

#include "core/geometry/Polyline.h"

#include <vector>

namespace lcad {

// Which side of the profile the tool actually cuts along, to compensate
// for the tool's own radius so the finished part comes out true to size:
// Outside keeps a part's outer boundary at its drawn size (cutting a
// blank OUT of stock), Inside keeps a hole/pocket's boundary at its drawn
// size (cutting material away FROM inside a boundary), OnLine cuts exactly
// on the drawn profile (no compensation -- typically for scoring/engraving).
enum class CutSide { OnLine, Outside, Inside };

struct ToolpathParams {
    double toolDiameter = 1.0;
    CutSide side = CutSide::OnLine;
    double feedRate = 1000.0;  // cutting feed, units/min
    double plungeRate = 300.0; // Z plunge feed, units/min
    double cutDepth = 1.0;     // total depth, machined in one or more stepDown passes
    double stepDown = 0.0;     // max Z removed per pass; <=0 or >= cutDepth means a single full-depth pass
    double safeHeight = 5.0;   // Z retract height between rapid moves
};

// Computes the actual cut path for profile: the profile's own (flattened --
// bulge arcs approximated as chords) vertices when side is OnLine, or a
// parallel copy offset by toolDiameter/2 (reusing offsetPolyline, see
// core/geometry/PolylineOps.h) toward the profile's centroid for Inside or
// away from it for Outside. Returns an empty vector if the offset
// degenerates (e.g. the tool is too large for a tight inside corner).
std::vector<Point2D> computeToolpath(const PolylineEntity& profile, const ToolpathParams& params);

} // namespace lcad
