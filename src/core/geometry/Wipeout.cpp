#include "core/geometry/Wipeout.h"

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

bool WipeoutEntity::containsPoint(const Point2D& pt) const {
    bool inside = false;
    const std::size_t n = m_vertices.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Point2D& vi = m_vertices[i];
        const Point2D& vj = m_vertices[j];
        if ((vi.y > pt.y) != (vj.y > pt.y) && pt.x < (vj.x - vi.x) * (pt.y - vi.y) / (vj.y - vi.y) + vi.x) {
            inside = !inside;
        }
    }
    return inside;
}

BoundingBox WipeoutEntity::boundingBox() const {
    BoundingBox box;
    for (const auto& v : m_vertices) box.expand(v);
    return box;
}

double WipeoutEntity::distanceTo(const Point2D& pt) const {
    if (m_vertices.size() >= 3 && containsPoint(pt)) return 0.0;
    double best = std::numeric_limits<double>::max();
    const std::size_t n = m_vertices.size();
    for (std::size_t i = 0; i < n; ++i) {
        best = std::min(best, distanceToSegment(pt, m_vertices[i], m_vertices[(i + 1) % n]));
    }
    return best;
}

void WipeoutEntity::translate(const Point2D& delta) {
    for (auto& v : m_vertices) v = v + delta;
}

void WipeoutEntity::rotate(const Point2D& center, double angleRadians) {
    for (auto& v : m_vertices) v = rotateAround(v, center, angleRadians);
}

void WipeoutEntity::scale(const Point2D& center, double factor) {
    for (auto& v : m_vertices) v = scaleAround(v, center, factor);
}

void WipeoutEntity::mirror(const Point2D& a, const Point2D& b) {
    for (auto& v : m_vertices) v = mirrorAcross(v, a, b);
}

std::vector<Point2D> WipeoutEntity::gripPoints() const {
    return m_vertices;
}

void WipeoutEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index < m_vertices.size()) m_vertices[index] = newPos;
}

std::vector<SnapPoint> WipeoutEntity::snapCandidates() const {
    std::vector<SnapPoint> result;
    for (const auto& v : m_vertices) result.push_back({v, SnapKind::Endpoint});
    return result;
}

std::unique_ptr<Entity> WipeoutEntity::clone() const {
    return std::make_unique<WipeoutEntity>(*this);
}

} // namespace lcad
