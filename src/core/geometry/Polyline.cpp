#include "core/geometry/Polyline.h"

#include <algorithm>
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

} // namespace

BoundingBox PolylineEntity::boundingBox() const {
    BoundingBox box;
    for (const auto& v : m_vertices) box.expand(v);
    return box;
}

double PolylineEntity::distanceTo(const Point2D& pt) const {
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i + 1 < m_vertices.size(); ++i) {
        best = std::min(best, distanceToSegment(pt, m_vertices[i], m_vertices[i + 1]));
    }
    if (m_closed && m_vertices.size() > 1) {
        best = std::min(best, distanceToSegment(pt, m_vertices.back(), m_vertices.front()));
    }
    return best;
}

void PolylineEntity::translate(const Point2D& delta) {
    for (auto& v : m_vertices) v = v + delta;
}

void PolylineEntity::rotate(const Point2D& center, double angleRadians) {
    for (auto& v : m_vertices) v = rotateAround(v, center, angleRadians);
}

void PolylineEntity::scale(const Point2D& center, double factor) {
    for (auto& v : m_vertices) v = scaleAround(v, center, factor);
}

std::vector<Point2D> PolylineEntity::gripPoints() const {
    return m_vertices;
}

void PolylineEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index < m_vertices.size()) m_vertices[index] = newPos;
}

std::vector<SnapPoint> PolylineEntity::snapCandidates() const {
    std::vector<SnapPoint> result;
    for (const auto& v : m_vertices) result.push_back({v, SnapKind::Endpoint});
    for (std::size_t i = 0; i + 1 < m_vertices.size(); ++i) {
        result.push_back({m_vertices[i] + (m_vertices[i + 1] - m_vertices[i]) * 0.5, SnapKind::Midpoint});
    }
    if (m_closed && m_vertices.size() > 1) {
        result.push_back({m_vertices.back() + (m_vertices.front() - m_vertices.back()) * 0.5, SnapKind::Midpoint});
    }
    return result;
}

std::unique_ptr<Entity> PolylineEntity::clone() const {
    return std::make_unique<PolylineEntity>(*this);
}

} // namespace lcad
