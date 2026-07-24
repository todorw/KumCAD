#pragma once

#include "core/geometry/Entity.h"

namespace lcad {

// A bus entry (KiCad-style): a short diagonal stub connecting a single
// wire's endpoint onto a BusEntity's run, drawn at 45 degrees by
// convention. Electrically it behaves exactly like a 2-point WireEntity --
// its start and end are a normal connectivity edge (see
// core/schematic/Netlist.h) -- geometrically it's the same shape as a
// LineEntity. It's kept as its own entity type (rather than reusing
// WireEntity or LineEntity) so ERC and rendering can tell "this is the
// diagonal stub that lets a wire legally touch a bus" apart from an
// ordinary wire segment or a drafting line.
class BusEntryEntity : public Entity {
public:
    BusEntryEntity(EntityId id, LayerId layer, Point2D start, Point2D end)
        : Entity(id, layer), m_start(start), m_end(end) {}

    const Point2D& start() const { return m_start; }
    const Point2D& end() const { return m_end; }

    EntityType type() const override { return EntityType::BusEntry; }
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
    Point2D m_start;
    Point2D m_end;
};

} // namespace lcad
