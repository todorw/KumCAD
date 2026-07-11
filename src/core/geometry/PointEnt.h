#pragma once

#include "core/geometry/Entity.h"

namespace lcad {

// AutoCAD POINT: a single location, drawn per the document's point style
// (PDMODE/PDSIZE) and snapped via the NODe osnap. DIVIDE and MEASURE place
// these along curves.
class PointEntity : public Entity {
public:
    PointEntity(EntityId id, LayerId layer, Point2D position) : Entity(id, layer), m_position(position) {}

    const Point2D& position() const { return m_position; }

    EntityType type() const override { return EntityType::Point; }
    BoundingBox boundingBox() const override {
        BoundingBox box;
        box.expand(m_position);
        return box;
    }
    double distanceTo(const Point2D& pt) const override { return m_position.distanceTo(pt); }
    void translate(const Point2D& delta) override { m_position = m_position + delta; }
    void rotate(const Point2D& center, double angleRadians) override {
        m_position = rotateAround(m_position, center, angleRadians);
    }
    void scale(const Point2D& center, double factor) override {
        m_position = scaleAround(m_position, center, factor);
    }
    void mirror(const Point2D& a, const Point2D& b) override { m_position = mirrorAcross(m_position, a, b); }
    std::vector<Point2D> gripPoints() const override { return {m_position}; }
    void moveGripPoint(std::size_t index, const Point2D& newPos) override {
        if (index == 0) m_position = newPos;
    }
    std::vector<SnapPoint> snapCandidates() const override { return {{m_position, SnapKind::Node}}; }
    std::unique_ptr<Entity> clone() const override { return std::make_unique<PointEntity>(*this); }

private:
    Point2D m_position;
};

} // namespace lcad
