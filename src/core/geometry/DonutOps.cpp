#include "core/geometry/DonutOps.h"

#include <cmath>

namespace lcad {

namespace {

std::vector<Point2D> circleLoop(const Point2D& center, double radius, int segments, bool ccw) {
    std::vector<Point2D> verts;
    if (radius <= 0.0 || segments < 3) return verts;
    verts.reserve(static_cast<std::size_t>(segments));
    const double sign = ccw ? 1.0 : -1.0;
    for (int i = 0; i < segments; ++i) {
        const double angle = sign * (2.0 * M_PI * static_cast<double>(i)) / static_cast<double>(segments);
        verts.push_back(center + Point2D(std::cos(angle), std::sin(angle)) * radius);
    }
    return verts;
}

} // namespace

std::vector<RegionLoop> buildDonutLoops(const Point2D& center, double insideRadius, double outsideRadius,
                                        int segments) {
    std::vector<RegionLoop> loops;
    if (outsideRadius <= 0.0) return loops;

    RegionLoop outer;
    outer.vertices = circleLoop(center, outsideRadius, segments, /*ccw=*/true);
    if (outer.vertices.empty()) return loops;
    loops.push_back(std::move(outer));

    if (insideRadius > 1e-9 && insideRadius < outsideRadius) {
        RegionLoop hole;
        hole.vertices = circleLoop(center, insideRadius, segments, /*ccw=*/false);
        loops.push_back(std::move(hole));
    }
    return loops;
}

} // namespace lcad
