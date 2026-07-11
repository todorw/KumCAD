#pragma once

#include "core/geometry/Point2D.h"

#include <optional>
#include <utility>
#include <vector>

namespace lcad {

// AutoCAD HATCH's "pick point" boundary detection: given a soup of candidate
// boundary segments (already flattened from whatever entities -- lines,
// polyline edges, tessellated arcs/circles) and a point, finds the tightest
// closed loop of segments that encloses the point, tracing the planar
// arrangement's faces the way HATCH derives an implicit boundary rather than
// requiring a single pre-closed polyline.
//
// Returns nullopt if pickPoint isn't enclosed by any loop the segments form
// (open geometry, or nothing to the point's right at all).
std::optional<std::vector<Point2D>> traceBoundary(const std::vector<std::pair<Point2D, Point2D>>& segments,
                                                   const Point2D& pickPoint);

} // namespace lcad
