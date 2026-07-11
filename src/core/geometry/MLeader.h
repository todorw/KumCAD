#pragma once

#include "core/geometry/Entity.h"

#include <vector>

namespace lcad {

// AutoCAD MULTILEADER (simplified): one or more independent leader "legs",
// each a polyline from its own arrowhead to a landing point shared by every
// leg. The annotation itself is a separate MTEXT entity at the landing, like
// LeaderEntity. Legs beyond the first can only be authored by hand-edited DXF
// today -- the MLEADER command creates a single-leg multileader; adding legs
// to an existing one ("Add Leader") isn't implemented yet.
class MLeaderEntity : public Entity {
public:
    MLeaderEntity(EntityId id, LayerId layer, std::vector<std::vector<Point2D>> legs, Point2D landing,
                 double arrowSize = 1.25)
        : Entity(id, layer), m_legs(std::move(legs)), m_landing(landing), m_arrowSize(arrowSize) {}

    const std::vector<std::vector<Point2D>>& legs() const { return m_legs; }
    const Point2D& landing() const { return m_landing; }
    double arrowSize() const { return m_arrowSize; }

    EntityType type() const override { return EntityType::MLeader; }
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
    std::vector<std::vector<Point2D>> m_legs; // each: arrowhead .. last point before the shared landing
    Point2D m_landing;
    double m_arrowSize;
};

} // namespace lcad
