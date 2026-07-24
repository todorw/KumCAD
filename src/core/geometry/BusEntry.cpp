#include "core/geometry/BusEntry.h"

#include <algorithm>

namespace lcad {

BoundingBox BusEntryEntity::boundingBox() const {
    BoundingBox box;
    box.expand(m_start);
    box.expand(m_end);
    return box;
}

double BusEntryEntity::distanceTo(const Point2D& pt) const {
    const Point2D seg = m_end - m_start;
    const double lenSq = seg.dot(seg);
    if (lenSq < 1e-12) return pt.distanceTo(m_start);

    double t = (pt - m_start).dot(seg) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return pt.distanceTo(m_start + seg * t);
}

void BusEntryEntity::translate(const Point2D& delta) {
    m_start = m_start + delta;
    m_end = m_end + delta;
}

void BusEntryEntity::rotate(const Point2D& center, double angleRadians) {
    m_start = rotateAround(m_start, center, angleRadians);
    m_end = rotateAround(m_end, center, angleRadians);
}

void BusEntryEntity::scale(const Point2D& center, double factor) {
    m_start = scaleAround(m_start, center, factor);
    m_end = scaleAround(m_end, center, factor);
}

void BusEntryEntity::mirror(const Point2D& a, const Point2D& b) {
    m_start = mirrorAcross(m_start, a, b);
    m_end = mirrorAcross(m_end, a, b);
}

std::vector<Point2D> BusEntryEntity::gripPoints() const { return {m_start, m_end}; }

void BusEntryEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) {
        m_start = newPos;
    } else if (index == 1) {
        m_end = newPos;
    }
}

std::vector<SnapPoint> BusEntryEntity::snapCandidates() const {
    return {
        {m_start, SnapKind::Endpoint},
        {m_end, SnapKind::Endpoint},
    };
}

std::unique_ptr<Entity> BusEntryEntity::clone() const { return std::make_unique<BusEntryEntity>(*this); }

} // namespace lcad
