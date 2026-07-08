#pragma once

#include "core/geometry/Entity.h"

namespace lcad {

// Axis-aligned ellipse (no rotation support yet): center plus independent X
// and Y radii. rotate() only repositions the center about the given pivot,
// since a true rotated ellipse would need a stored rotation angle we don't
// have -- a deliberate simplification for the first pass at this entity.
class EllipseEntity : public Entity {
public:
    EllipseEntity(EntityId id, LayerId layer, Point2D center, double radiusX, double radiusY)
        : Entity(id, layer), m_center(center), m_radiusX(radiusX), m_radiusY(radiusY) {}

    const Point2D& center() const { return m_center; }
    double radiusX() const { return m_radiusX; }
    double radiusY() const { return m_radiusY; }

    EntityType type() const override { return EntityType::Ellipse; }
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
    Point2D m_center;
    double m_radiusX;
    double m_radiusY;
};

} // namespace lcad
