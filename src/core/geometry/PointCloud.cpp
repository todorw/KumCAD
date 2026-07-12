#include "core/geometry/PointCloud.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lcad {

BoundingBox PointCloudEntity::boundingBox() const {
    BoundingBox box;
    for (const Point2D& p : m_points) box.expand(p);
    return box;
}

double PointCloudEntity::distanceTo(const Point2D& pt) const {
    const BoundingBox box = boundingBox();
    if (!box.isValid()) return std::numeric_limits<double>::max();
    if (box.contains(pt)) return 0.0;
    const double dx = std::max({box.min.x - pt.x, pt.x - box.max.x, 0.0});
    const double dy = std::max({box.min.y - pt.y, pt.y - box.max.y, 0.0});
    return std::sqrt(dx * dx + dy * dy);
}

void PointCloudEntity::translate(const Point2D& delta) {
    for (Point2D& p : m_points) p = p + delta;
}

void PointCloudEntity::rotate(const Point2D& center, double angleRadians) {
    for (Point2D& p : m_points) p = rotateAround(p, center, angleRadians);
}

void PointCloudEntity::scale(const Point2D& center, double factor) {
    for (Point2D& p : m_points) p = scaleAround(p, center, factor);
}

void PointCloudEntity::mirror(const Point2D& a, const Point2D& b) {
    for (Point2D& p : m_points) p = mirrorAcross(p, a, b);
}

std::vector<Point2D> PointCloudEntity::gripPoints() const {
    const BoundingBox box = boundingBox();
    if (!box.isValid()) return {};
    return {Point2D((box.min.x + box.max.x) / 2.0, (box.min.y + box.max.y) / 2.0)};
}

void PointCloudEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index != 0) return;
    const auto grips = gripPoints();
    if (grips.empty()) return;
    translate(newPos - grips[0]);
}

std::unique_ptr<Entity> PointCloudEntity::clone() const { return std::make_unique<PointCloudEntity>(*this); }

} // namespace lcad
