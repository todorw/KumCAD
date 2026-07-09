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

Point2D EllipseEntity::localToWorld(double x, double y) const {
    const double c = std::cos(m_rotation);
    const double s = std::sin(m_rotation);
    return {m_center.x + x * c - y * s, m_center.y + x * s + y * c};
}

BoundingBox EllipseEntity::boundingBox() const {
    // Tight extents of a rotated ellipse along the world axes.
    const double c = std::cos(m_rotation);
    const double s = std::sin(m_rotation);
    const double ex = std::sqrt(m_radiusX * m_radiusX * c * c + m_radiusY * m_radiusY * s * s);
    const double ey = std::sqrt(m_radiusX * m_radiusX * s * s + m_radiusY * m_radiusY * c * c);
    BoundingBox box;
    box.expand(Point2D(m_center.x - ex, m_center.y - ey));
    box.expand(Point2D(m_center.x + ex, m_center.y + ey));
    return box;
}

double EllipseEntity::distanceTo(const Point2D& pt) const {
    double best = std::numeric_limits<double>::max();
    Point2D prev = localToWorld(m_radiusX, 0.0);
    for (int i = 1; i <= kDistanceSamples; ++i) {
        const double t = (2.0 * M_PI * i) / kDistanceSamples;
        const Point2D cur = localToWorld(m_radiusX * std::cos(t), m_radiusY * std::sin(t));
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
    m_rotation += angleRadians;
}

void EllipseEntity::scale(const Point2D& center, double factor) {
    m_center = scaleAround(m_center, center, factor);
    m_radiusX *= factor;
    m_radiusY *= factor;
}

void EllipseEntity::mirror(const Point2D& a, const Point2D& b) {
    m_center = mirrorAcross(m_center, a, b);
    // An ellipse is symmetric about both its axes, so reflecting the local
    // frame's angle is all that's needed.
    const double phi = std::atan2(b.y - a.y, b.x - a.x);
    m_rotation = 2.0 * phi - m_rotation;
}

std::vector<Point2D> EllipseEntity::gripPoints() const {
    return {
        m_center,
        localToWorld(m_radiusX, 0.0),
        localToWorld(-m_radiusX, 0.0),
        localToWorld(0.0, m_radiusY),
        localToWorld(0.0, -m_radiusY),
    };
}

void EllipseEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) {
        m_center = newPos;
        return;
    }
    // Axis grips resize along their own (possibly rotated) axis.
    const Point2D local = rotateAround(newPos, m_center, -m_rotation) - m_center;
    if (index == 1 || index == 2) {
        m_radiusX = std::abs(local.x);
    } else if (index == 3 || index == 4) {
        m_radiusY = std::abs(local.y);
    }
}

std::vector<SnapPoint> EllipseEntity::snapCandidates() const {
    return {
        {m_center, SnapKind::Center},
        {localToWorld(m_radiusX, 0.0), SnapKind::Quadrant},
        {localToWorld(-m_radiusX, 0.0), SnapKind::Quadrant},
        {localToWorld(0.0, m_radiusY), SnapKind::Quadrant},
        {localToWorld(0.0, -m_radiusY), SnapKind::Quadrant},
    };
}

std::unique_ptr<Entity> EllipseEntity::clone() const {
    return std::make_unique<EllipseEntity>(*this);
}

} // namespace lcad
