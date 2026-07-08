#include "core/geometry/Ellipse.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lcad {

namespace {

double distanceToSegment(const Point2D& pt, const Point2D& a, const Point2D& b) {
    const Point2D seg = b - a;
    const double lenSq = seg.dot(seg);
    if (lenSq < 1e-12) return pt.distanceTo(a);
    double t = (pt - a).dot(seg) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return pt.distanceTo(a + seg * t);
}

// Hit-testing an ellipse exactly requires solving a quartic, which is
// overkill for click-picking -- sample the perimeter as a polygon instead.
constexpr int kDistanceSamples = 64;

} // namespace

BoundingBox EllipseEntity::boundingBox() const {
    BoundingBox box;
    box.expand(Point2D(m_center.x - m_radiusX, m_center.y - m_radiusY));
    box.expand(Point2D(m_center.x + m_radiusX, m_center.y + m_radiusY));
    return box;
}

double EllipseEntity::distanceTo(const Point2D& pt) const {
    double best = std::numeric_limits<double>::max();
    Point2D prev(m_center.x + m_radiusX, m_center.y);
    for (int i = 1; i <= kDistanceSamples; ++i) {
        const double t = (2.0 * M_PI * i) / kDistanceSamples;
        const Point2D cur(m_center.x + m_radiusX * std::cos(t), m_center.y + m_radiusY * std::sin(t));
        best = std::min(best, distanceToSegment(pt, prev, cur));
        prev = cur;
    }
    return best;
}

void EllipseEntity::translate(const Point2D& delta) {
    m_center = m_center + delta;
}

void EllipseEntity::rotate(const Point2D& center, double angleRadians) {
    m_center = rotateAround(m_center, center, angleRadians);
}

void EllipseEntity::scale(const Point2D& center, double factor) {
    m_center = scaleAround(m_center, center, factor);
    m_radiusX *= factor;
    m_radiusY *= factor;
}

std::vector<Point2D> EllipseEntity::gripPoints() const {
    return {
        m_center,
        Point2D(m_center.x + m_radiusX, m_center.y),
        Point2D(m_center.x - m_radiusX, m_center.y),
        Point2D(m_center.x, m_center.y + m_radiusY),
        Point2D(m_center.x, m_center.y - m_radiusY),
    };
}

void EllipseEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) {
        m_center = newPos;
    } else if (index == 1 || index == 2) {
        m_radiusX = std::abs(newPos.x - m_center.x);
    } else if (index == 3 || index == 4) {
        m_radiusY = std::abs(newPos.y - m_center.y);
    }
}

std::vector<SnapPoint> EllipseEntity::snapCandidates() const {
    return {
        {m_center, SnapKind::Center},
        {Point2D(m_center.x + m_radiusX, m_center.y), SnapKind::Quadrant},
        {Point2D(m_center.x - m_radiusX, m_center.y), SnapKind::Quadrant},
        {Point2D(m_center.x, m_center.y + m_radiusY), SnapKind::Quadrant},
        {Point2D(m_center.x, m_center.y - m_radiusY), SnapKind::Quadrant},
    };
}

std::unique_ptr<Entity> EllipseEntity::clone() const {
    return std::make_unique<EllipseEntity>(*this);
}

} // namespace lcad
