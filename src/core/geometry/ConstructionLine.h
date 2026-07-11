#pragma once

#include "core/geometry/Entity.h"

namespace lcad {

// AutoCAD XLINE (infinite line) / RAY (half-infinite): a base point and a
// unit direction. Excluded from drawing extents (boundingBox is just the
// base point) so Zoom Extents ignores them, like AutoCAD.
class ConstructionLineEntity : public Entity {
public:
    ConstructionLineEntity(EntityId id, LayerId layer, Point2D point, Point2D direction, bool ray)
        : Entity(id, layer), m_point(point), m_direction(normalized(direction)), m_ray(ray) {}

    const Point2D& basePoint() const { return m_point; }
    const Point2D& direction() const { return m_direction; }
    bool isRay() const { return m_ray; }

    // A long finite segment standing in for the infinite extent — what
    // rendering and intersection tests actually use.
    void asSegment(Point2D& a, Point2D& b) const {
        constexpr double kFar = 1e6;
        a = m_ray ? m_point : m_point - m_direction * kFar;
        b = m_point + m_direction * kFar;
    }

    EntityType type() const override { return EntityType::ConstructionLine; }
    BoundingBox boundingBox() const override {
        BoundingBox box;
        box.expand(m_point);
        return box;
    }
    double distanceTo(const Point2D& pt) const override {
        const Point2D d = pt - m_point;
        double along = d.dot(m_direction);
        if (m_ray && along < 0) along = 0;
        return pt.distanceTo(m_point + m_direction * along);
    }
    void translate(const Point2D& delta) override { m_point = m_point + delta; }
    void rotate(const Point2D& center, double angleRadians) override {
        m_point = rotateAround(m_point, center, angleRadians);
        m_direction = rotateAround(m_direction, Point2D(), angleRadians);
    }
    void scale(const Point2D& center, double factor) override { m_point = scaleAround(m_point, center, factor); }
    void mirror(const Point2D& a, const Point2D& b) override {
        m_point = mirrorAcross(m_point, a, b);
        m_direction = normalized(mirrorAcross(m_direction, Point2D(), b - a));
    }
    std::vector<Point2D> gripPoints() const override { return {m_point, m_point + m_direction * 10.0}; }
    void moveGripPoint(std::size_t index, const Point2D& newPos) override {
        if (index == 0) {
            m_point = newPos;
        } else if (index == 1) {
            m_direction = normalized(newPos - m_point);
        }
    }
    std::vector<SnapPoint> snapCandidates() const override { return {{m_point, SnapKind::Endpoint}}; }
    std::unique_ptr<Entity> clone() const override { return std::make_unique<ConstructionLineEntity>(*this); }

private:
    static Point2D normalized(const Point2D& v) {
        const double len = v.length();
        return len > 1e-12 ? v * (1.0 / len) : Point2D(1.0, 0.0);
    }

    Point2D m_point;
    Point2D m_direction;
    bool m_ray;
};

} // namespace lcad
