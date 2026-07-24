#include "core/geometry/PolygonOps.h"

#include <cmath>

namespace lcad {

std::vector<Point2D> regularPolygonVertices(const Point2D& center, double radius, int sides, bool inscribed,
                                            double startAngleRadians) {
    std::vector<Point2D> verts;
    if (sides < 3 || radius <= 0.0) return verts;

    const double vertexRadius = inscribed ? radius : radius / std::cos(M_PI / sides);
    verts.reserve(static_cast<std::size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const double angle = startAngleRadians + (2.0 * M_PI * static_cast<double>(i)) / static_cast<double>(sides);
        verts.push_back(center + Point2D(std::cos(angle), std::sin(angle)) * vertexRadius);
    }
    return verts;
}

} // namespace lcad
