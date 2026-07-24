#include "core/geometry/ChamferOps.h"

#include <cmath>

namespace lcad {

namespace {

std::optional<Point2D> infiniteIntersection(const LineEntity& l1, const LineEntity& l2) {
    const Point2D r = l1.end() - l1.start();
    const Point2D s = l2.end() - l2.start();
    const double denom = r.x * s.y - r.y * s.x;
    if (std::abs(denom) < 1e-12) return std::nullopt;
    const Point2D qp = l2.start() - l1.start();
    const double t = (qp.x * s.y - qp.y * s.x) / denom;
    return l1.start() + r * t;
}

Point2D normalized(const Point2D& v) {
    const double len = v.length();
    return len > 1e-12 ? v * (1.0 / len) : Point2D(1, 0);
}

} // namespace

std::optional<ChamferGeometry> computeChamferGeometry(const LineEntity& line1, const LineEntity& line2,
                                                       double distance1, double distance2) {
    if (distance1 < 0.0 || distance2 < 0.0) return std::nullopt;

    const auto cornerOpt = infiniteIntersection(line1, line2);
    if (!cornerOpt) return std::nullopt;
    const Point2D corner = *cornerOpt;

    const bool keepEnd1 = corner.distanceTo(line1.end()) >= corner.distanceTo(line1.start());
    const bool keepEnd2 = corner.distanceTo(line2.end()) >= corner.distanceTo(line2.start());
    const Point2D far1 = keepEnd1 ? line1.end() : line1.start();
    const Point2D far2 = keepEnd2 ? line2.end() : line2.start();

    const double len1 = corner.distanceTo(far1);
    const double len2 = corner.distanceTo(far2);
    if (distance1 > len1 - 1e-9 || distance2 > len2 - 1e-9) return std::nullopt;

    ChamferGeometry result;
    result.trim1 = corner + normalized(far1 - corner) * distance1;
    result.trim2 = corner + normalized(far2 - corner) * distance2;
    result.keepEnd1 = keepEnd1;
    result.keepEnd2 = keepEnd2;
    return result;
}

} // namespace lcad
