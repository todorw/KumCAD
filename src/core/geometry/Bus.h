#pragma once

#include "core/geometry/Entity.h"

#include <string>
#include <vector>

namespace lcad {

// A schematic bus (KiCad-style): a bundled group of related signals drawn
// as one thick line so a schematic doesn't need N parallel wires for
// something like an 8-bit data bus. Geometrically it's a straight-segment
// polyline exactly like WireEntity, but it does NOT itself carry
// connectivity the way a wire does -- a bus line by itself never merges two
// touching points into the same net. Real connectivity for each individual
// signal comes from the member wire stubs (connected to the bus via
// BusEntryEntity) each carrying its own NetLabelEntity name, which is what
// actually merges same-named points into a net -- the existing NetLabel
// machinery already does that with zero bus-specific code. The bus line and
// its optional declared name (e.g. "DATA[0..7]") are purely organizational/
// visual: a hint for humans (and ERC) about which individual member names
// are expected to run through this bundle.
class BusEntity : public Entity {
public:
    BusEntity(EntityId id, LayerId layer, std::vector<Point2D> vertices, std::string name = {})
        : Entity(id, layer), m_vertices(std::move(vertices)), m_name(std::move(name)) {}

    const std::vector<Point2D>& vertices() const { return m_vertices; }
    // The bus's own declared name, e.g. "DATA[0..7]" -- empty if undeclared.
    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    EntityType type() const override { return EntityType::Bus; }
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
    std::vector<Point2D> m_vertices;
    std::string m_name;
};

} // namespace lcad
