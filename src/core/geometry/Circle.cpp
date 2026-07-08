#include "core/geometry/Circle.h"

#include <cmath>

namespace lcad {

BoundingBox CircleEntity::boundingBox() const {
    BoundingBox box;
    box.expand(Point2D(m_center.x - m_radius, m_center.y - m_radius));
    box.expand(Point2D(m_center.x + m_radius, m_center.y + m_radius));
    return box;
}

double CircleEntity::distanceTo(const Point2D& pt) const {
    return std::abs(pt.distanceTo(m_center) - m_radius);
}

void CircleEntity::translate(const Point2D& delta) {
    m_center = m_center + delta;
}

void CircleEntity::rotate(const Point2D& center, double angleRadians) {
    m_center = rotateAround(m_center, center, angleRadians);
}

void CircleEntity::scale(const Point2D& center, double factor) {
    m_center = scaleAround(m_center, center, factor);
    m_radius *= factor;
}

std::vector<Point2D> CircleEntity::gripPoints() const {
    return {m_center, Point2D(m_center.x + m_radius, m_center.y)};
}

void CircleEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) {
        m_center = newPos;
    } else if (index == 1) {
        m_radius = m_center.distanceTo(newPos);
    }
}

std::vector<SnapPoint> CircleEntity::snapCandidates() const {
    return {
        {m_center, SnapKind::Center},
        {Point2D(m_center.x + m_radius, m_center.y), SnapKind::Quadrant},
        {Point2D(m_center.x, m_center.y + m_radius), SnapKind::Quadrant},
        {Point2D(m_center.x - m_radius, m_center.y), SnapKind::Quadrant},
        {Point2D(m_center.x, m_center.y - m_radius), SnapKind::Quadrant},
    };
}

std::unique_ptr<Entity> CircleEntity::clone() const {
    return std::make_unique<CircleEntity>(*this);
}

} // namespace lcad
