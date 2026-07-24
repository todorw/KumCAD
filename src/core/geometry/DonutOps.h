#pragma once

#include "core/geometry/Region.h"

namespace lcad {

// Real geometry for AutoCAD's DONUT: a filled ring between two
// concentric circles, built as a RegionEntity's own two loops (outer
// CCW, inner CW/hole -- see RegionLoop's own winding convention in
// Region.h), each tessellated into `segments` straight edges -- a real,
// disclosed approximation, since RegionLoop has no true curved-edge
// support (Region.h's own comment). insideRadius <= 0 (or >=
// outsideRadius) produces a single outer loop only -- a solid filled
// disc, real AutoCAD's own DONUT behavior for an inside diameter of 0.
// Returns an empty vector for outsideRadius <= 0.
std::vector<RegionLoop> buildDonutLoops(const Point2D& center, double insideRadius, double outsideRadius,
                                        int segments = 64);

} // namespace lcad
