#pragma once

#include "core/geometry/Line.h"

#include <optional>

namespace lcad {

// Real geometry for AutoCAD's CHAMFER, distance-mode: the two lines'
// intersection point is computed from their own infinite extensions (as
// if untrimmed), then cut back distance1 along line1 and distance2 along
// line2, each measured from that intersection point toward that line's
// own FAR endpoint (the one geometrically farther from the intersection
// -- the one a real corner-cleanup keeps, mirroring FilletCommand's own
// "keep the far endpoint" convention). The two trim points become the
// endpoints of a new straight chamfer segment.
struct ChamferGeometry {
    Point2D trim1;    // line1's new endpoint (replaces its NEAR one)
    Point2D trim2;    // line2's new endpoint
    bool keepEnd1;    // true if line1's END (not start) is the far/kept endpoint
    bool keepEnd2;
};

// Returns nullopt if the lines are parallel (collinear included, since a
// chamfer needs a real corner), or if distance1/distance2 is negative or
// exceeds that line's own length from the intersection to its far
// endpoint (nothing left to chamfer from).
std::optional<ChamferGeometry> computeChamferGeometry(const LineEntity& line1, const LineEntity& line2,
                                                       double distance1, double distance2);

} // namespace lcad
