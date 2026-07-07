#pragma once

#include "core/Ids.h"
#include "core/geometry/BoundingBox.h"
#include "core/geometry/Point2D.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace lcad {

enum class EntityType {
    Line,
    Circle,
    Arc,
    Polyline,
};

class Entity {
public:
    Entity(EntityId id, LayerId layer) : m_id(id), m_layer(layer) {}
    virtual ~Entity() = default;

    EntityId id() const { return m_id; }
    void setId(EntityId id) { m_id = id; }
    LayerId layer() const { return m_layer; }
    void setLayer(LayerId layer) { m_layer = layer; }

    virtual EntityType type() const = 0;
    virtual BoundingBox boundingBox() const = 0;

    // Shortest distance from pt to this entity, used for click picking.
    virtual double distanceTo(const Point2D& pt) const = 0;

    // Rigid move by delta, used for click-drag repositioning.
    virtual void translate(const Point2D& delta) = 0;

    // Rigid rotation about center, used by the ROTATE command.
    virtual void rotate(const Point2D& center, double angleRadians) = 0;

    // Uniform scale about center, used by the SCALE command.
    virtual void scale(const Point2D& center, double factor) = 0;

    // Key points shown as draggable grip handles when the entity is selected.
    virtual std::vector<Point2D> gripPoints() const = 0;

    // Reshape the entity by moving a single grip (index into gripPoints()) to newPos.
    virtual void moveGripPoint(std::size_t index, const Point2D& newPos) = 0;

    virtual std::unique_ptr<Entity> clone() const = 0;

private:
    EntityId m_id;
    LayerId m_layer;
};

} // namespace lcad
