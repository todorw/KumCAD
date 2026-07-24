#include "core/geometry/RegionBuild.h"

#include "core/geometry/Circle.h"
#include "core/geometry/Polyline.h"

#include <cmath>

namespace lcad {

std::optional<std::vector<Point2D>> closedCurveToRegionLoop(const Entity& entity, int circleSegments) {
    if (entity.type() == EntityType::Circle) {
        const auto& circle = static_cast<const CircleEntity&>(entity);
        if (circle.radius() <= 0.0 || circleSegments < 3) return std::nullopt;
        std::vector<Point2D> verts;
        verts.reserve(static_cast<std::size_t>(circleSegments));
        for (int i = 0; i < circleSegments; ++i) {
            const double angle = (2.0 * M_PI * static_cast<double>(i)) / static_cast<double>(circleSegments);
            verts.push_back(circle.center() + Point2D(std::cos(angle), std::sin(angle)) * circle.radius());
        }
        return verts;
    }

    if (entity.type() == EntityType::Polyline) {
        const auto& polyline = static_cast<const PolylineEntity&>(entity);
        if (!polyline.closed()) return std::nullopt;
        std::vector<Point2D> verts = polyline.flattenedVertices();
        if (verts.size() < 3) return std::nullopt;
        return verts;
    }

    return std::nullopt;
}

} // namespace lcad
