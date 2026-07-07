#pragma once

#include "core/geometry/Entity.h"

namespace lcad {

class CircleEntity : public Entity {
public:
    CircleEntity(EntityId id, LayerId layer, Point2D center, double radius)
        : Entity(id, layer), m_center(center), m_radius(radius) {}

    const Point2D& center() const { return m_center; }
    double radius() const { return m_radius; }

    EntityType type() const override { return EntityType::Circle; }
    BoundingBox boundingBox() const override;
    double distanceTo(const Point2D& pt) const override;
    void translate(const Point2D& delta) override;
    void rotate(const Point2D& center, double angleRadians) override;
    void scale(const Point2D& center, double factor) override;
    std::vector<Point2D> gripPoints() const override;
    void moveGripPoint(std::size_t index, const Point2D& newPos) override;
    std::unique_ptr<Entity> clone() const override;

private:
    Point2D m_center;
    double m_radius;
};

} // namespace lcad
