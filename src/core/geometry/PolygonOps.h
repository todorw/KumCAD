#pragma once

#include "core/geometry/Point2D.h"

#include <vector>

namespace lcad {

// Real geometry for AutoCAD's POLYGON: `sides` vertices of a regular
// polygon centered at center, evenly spaced starting at startAngleRadians
// (world +X = 0, CCW) -- the caller picks that angle (0 for a typed
// radius, atan2(click - center) for a click-defined one, matching real
// AutoCAD's own two entry modes), this function only builds the real
// vertex geometry from it.
//
// inscribed=true: radius is the vertex distance from center (the
// polygon sits INSIDE a circle of that radius). inscribed=false: radius
// is the apothem (perpendicular distance from center to each edge's own
// midpoint -- the polygon is CIRCUMSCRIBED about a circle of that
// radius); the real vertex radius is then radius/cos(pi/sides), always
// >= the apothem. Returns an empty vector for sides < 3 or radius <= 0.
std::vector<Point2D> regularPolygonVertices(const Point2D& center, double radius, int sides, bool inscribed,
                                            double startAngleRadians = 0.0);

} // namespace lcad
