#pragma once

#include "core/geometry/Entity.h"

namespace lcad {

// Ellipse: center, radii along the ellipse's own local X/Y axes, and a
// rotation (radians, CCW) of that local frame relative to the world. An
// axis-aligned ellipse has rotation 0.
class EllipseEntity : public Entity {
public:
    EllipseEntity(EntityId id, LayerId layer, Point2D center, double radiusX, double radiusY,
                  double rotationRadians = 0.0)
        : Entity(id, layer), m_center(center), m_radiusX(radiusX), m_radiusY(radiusY), m_rotation(rotationRadians) {}

    const Point2D& center() const { return m_center; }
    double radiusX() const { return m_radiusX; }
    double radiusY() const { return m_radiusY; }
    double rotation() const { return m_rotation; }

    EntityType type() const override { return EntityType::Ellipse; }
    BoundingBox boundingBox() const override;
    double distanceTo(const Point2D& pt) const override;
    void translate(const Point2D& delta) override;
    void rotate(const Point2D& center, double angleRadians) override;
    void scale(const Point2D& center, double factor) override;
    void mirror(const Point2D& a, const Point2D& b) override;
    std::vector<Point2D> gripPoints() const override;
    void moveGripPoint(std::size_t index, const Point2D& newPos) override;
    std::vector<SnapPoint> snapCandidates() const override;
    std::unique_ptr<Entity> clone() const override;

private:
    // World position of the point at local coordinates (x, y).
    Point2D localToWorld(double x, double y) const;

    Point2D m_center;
    double m_radiusX;
    double m_radiusY;
    double m_rotation = 0.0;
};

} // namespace lcad
