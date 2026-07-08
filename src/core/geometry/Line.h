#pragma once

#include "core/geometry/Entity.h"

namespace lcad {

class LineEntity : public Entity {
public:
    LineEntity(EntityId id, LayerId layer, Point2D start, Point2D end)
        : Entity(id, layer), m_start(start), m_end(end) {}

    const Point2D& start() const { return m_start; }
    const Point2D& end() const { return m_end; }

    EntityType type() const override { return EntityType::Line; }
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
    Point2D m_start;
    Point2D m_end;
};

} // namespace lcad
