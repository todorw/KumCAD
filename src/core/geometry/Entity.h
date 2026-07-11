#pragma once

#include "core/Color.h"
#include "core/Ids.h"
#include "core/document/LineType.h"
#include "core/geometry/BoundingBox.h"
#include "core/geometry/Point2D.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace lcad {

enum class EntityType {
    Line,
    Circle,
    Arc,
    Polyline,
    Ellipse,
    Spline,
    Text,
    MText,
    Dimension,
    Leader,
    MLeader,
    Hatch,
    Insert,
    Point,            // POINT node (PDMODE/PDSIZE styled)
    ConstructionLine, // XLINE / RAY
    AttDef,           // attribute definition inside a block
    Table,            // TABLE grid of cell text
};

// Object-snap candidate kinds, mirroring AutoCAD's OSNAP markers. The first
// four are static per-entity points (Entity::snapCandidates()); the rest are
// computed against the cursor or the active command's anchor by the snap
// engine and never appear in a SnapRef.
enum class SnapKind {
    Endpoint,
    Midpoint,
    Center,
    Quadrant,
    Node,          // point entities
    Intersection,  // curve-curve crossing near the cursor
    Perpendicular, // foot of perpendicular from the command's anchor
    Tangent,       // tangent touch point from the command's anchor
    Nearest,       // closest point on the entity under the cursor
};
constexpr int kSnapKindCount = 9;

struct SnapPoint {
    Point2D point;
    SnapKind kind;
};

// A durable reference to one of an entity's snap points: "the index-th
// candidate of this kind". Associative dimensions store these so their
// definition points can follow the referenced entity when it is edited.
struct SnapRef {
    EntityId entityId = 0;
    SnapKind kind = SnapKind::Endpoint;
    int index = 0; // among the entity's snapCandidates() of this kind

    bool operator==(const SnapRef&) const = default;
};

class Entity {
public:
    Entity(EntityId id, LayerId layer) : m_id(id), m_layer(layer) {}
    virtual ~Entity() = default;

    EntityId id() const { return m_id; }
    void setId(EntityId id) { m_id = id; }
    LayerId layer() const { return m_layer; }
    void setLayer(LayerId layer) { m_layer = layer; }

    // Explicit color, overriding the layer's ("ByLayer" when unset).
    const std::optional<Color>& colorOverride() const { return m_colorOverride; }
    void setColorOverride(std::optional<Color> color) { m_colorOverride = color; }

    // Explicit linetype, overriding the layer's ("ByLayer" when unset).
    const std::optional<LineType>& linetypeOverride() const { return m_linetypeOverride; }
    void setLinetypeOverride(std::optional<LineType> linetype) { m_linetypeOverride = linetype; }

    // Explicit lineweight in mm, overriding the layer's ("ByLayer" when
    // unset). Display honors it when LWDISPLAY is on.
    const std::optional<double>& lineweightOverride() const { return m_lineweightOverride; }
    void setLineweightOverride(std::optional<double> weight) { m_lineweightOverride = weight; }

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

    // Reflects the entity across the line through a and b, used by the MIRROR
    // command. Implementations must preserve their own invariants (e.g. an
    // arc's CCW start-to-end sweep convention).
    virtual void mirror(const Point2D& a, const Point2D& b) = 0;

    // Key points shown as draggable grip handles when the entity is selected.
    virtual std::vector<Point2D> gripPoints() const = 0;

    // Reshape the entity by moving a single grip (index into gripPoints()) to newPos.
    virtual void moveGripPoint(std::size_t index, const Point2D& newPos) = 0;

    // Points offered to the object-snap engine while picking a point for a
    // command (endpoints, midpoints, centers, quadrants).
    virtual std::vector<SnapPoint> snapCandidates() const = 0;

    virtual std::unique_ptr<Entity> clone() const = 0;

private:
    EntityId m_id;
    LayerId m_layer;
    std::optional<Color> m_colorOverride;
    std::optional<LineType> m_linetypeOverride;
    std::optional<double> m_lineweightOverride;
};

} // namespace lcad
