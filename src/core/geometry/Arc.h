#pragma once

#include "core/geometry/Entity.h"

namespace lcad {

// Angles are in radians, measured counter-clockwise from +X, matching AutoCAD's
// convention for ARC start/end angles. The arc always sweeps counter-clockwise
// from startAngle to endAngle (wrapping past 0 if endAngle < startAngle).
class ArcEntity : public Entity {
public:
    ArcEntity(EntityId id, LayerId layer, Point2D center, double radius, double startAngle, double endAngle)
        : Entity(id, layer), m_center(center), m_radius(radius), m_startAngle(startAngle), m_endAngle(endAngle) {}

    const Point2D& center() const { return m_center; }
    double radius() const { return m_radius; }
    double startAngle() const { return m_startAngle; }
    double endAngle() const { return m_endAngle; }

    Point2D startPoint() const;
    Point2D endPoint() const;

    EntityType type() const override { return EntityType::Arc; }
    BoundingBox boundingBox() const override;
    double distanceTo(const Point2D& pt) const override;
    void translate(const Point2D& delta) override;
    void rotate(const Point2D& center, double angleRadians) override;
    void scale(const Point2D& center, double factor) override;
    std::vector<Point2D> gripPoints() const override;
    void moveGripPoint(std::size_t index, const Point2D& newPos) override;
    std::vector<SnapPoint> snapCandidates() const override;
    std::unique_ptr<Entity> clone() const override;

private:
    bool angleInSweep(double angle) const;

    Point2D m_center;
    double m_radius;
    double m_startAngle;
    double m_endAngle;
};

} // namespace lcad
