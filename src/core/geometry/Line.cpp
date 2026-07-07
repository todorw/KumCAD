#include "core/geometry/Line.h"

#include <algorithm>

namespace lcad {

BoundingBox LineEntity::boundingBox() const {
    BoundingBox box;
    box.expand(m_start);
    box.expand(m_end);
    return box;
}

double LineEntity::distanceTo(const Point2D& pt) const {
    const Point2D seg = m_end - m_start;
    const double lenSq = seg.dot(seg);
    if (lenSq < 1e-12) return pt.distanceTo(m_start);

    double t = (pt - m_start).dot(seg) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return pt.distanceTo(m_start + seg * t);
}

void LineEntity::translate(const Point2D& delta) {
    m_start = m_start + delta;
    m_end = m_end + delta;
}

void LineEntity::rotate(const Point2D& center, double angleRadians) {
    m_start = rotateAround(m_start, center, angleRadians);
    m_end = rotateAround(m_end, center, angleRadians);
}

void LineEntity::scale(const Point2D& center, double factor) {
    m_start = scaleAround(m_start, center, factor);
    m_end = scaleAround(m_end, center, factor);
}

std::vector<Point2D> LineEntity::gripPoints() const {
    return {m_start, m_end, m_start + (m_end - m_start) * 0.5};
}

void LineEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) {
        m_start = newPos;
    } else if (index == 1) {
        m_end = newPos;
    } else if (index == 2) {
        const Point2D mid = m_start + (m_end - m_start) * 0.5;
        translate(newPos - mid);
    }
}

std::unique_ptr<Entity> LineEntity::clone() const {
    return std::make_unique<LineEntity>(*this);
}

} // namespace lcad
