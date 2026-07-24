#pragma once

#include "core/geometry/Entity.h"
#include "core/geometry/Point2D.h"

#include <optional>
#include <vector>

namespace lcad {

// Converts entity into a single closed polygon loop suitable for a new
// RegionEntity, matching real AutoCAD's REGION command consuming a
// closed 2D boundary curve -- a closed PolylineEntity (via its own
// flattenedVertices(), tessellating any bulge/arc segments into short
// chords -- the same real, disclosed approximation RegionLoop's own
// straight-edges-only model already accepts) or a CircleEntity
// (tessellated into circleSegments points). Returns nullopt for an open
// polyline, a polyline with fewer than 3 vertices, or any other entity
// type -- real AutoCAD REGION also accepts closed splines/ellipses/
// arcs-forming-a-loop; this codebase's own disclosed subset stops at
// polylines and circles, the two closed-curve types it already models
// as single self-contained entities.
std::optional<std::vector<Point2D>> closedCurveToRegionLoop(const Entity& entity, int circleSegments = 64);

} // namespace lcad
