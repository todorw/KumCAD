#pragma once

#include "core/geometry/Entity.h"

#include <vector>

namespace lcad {

class PolylineEntity : public Entity {
public:
    PolylineEntity(EntityId id, LayerId layer, std::vector<Point2D> vertices, bool closed = false)
        : Entity(id, layer), m_vertices(std::move(vertices)), m_closed(closed) {}

    const std::vector<Point2D>& vertices() const { return m_vertices; }
    bool closed() const { return m_closed; }

    EntityType type() const override { return EntityType::Polyline; }
    BoundingBox boundingBox() const override;
    double distanceTo(const Point2D& pt) const override;
    void translate(const Point2D& delta) override;
    std::vector<Point2D> gripPoints() const override;
    void moveGripPoint(std::size_t index, const Point2D& newPos) override;
    std::unique_ptr<Entity> clone() const override;

private:
    std::vector<Point2D> m_vertices;
    bool m_closed;
};

} // namespace lcad
