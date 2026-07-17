#pragma once

#include "core/geometry/Entity.h"

#include <algorithm>

namespace lcad {

// A PCB via: a drilled, plated hole connecting copper layers at this
// position. Without a CopperStackup (see core/pcb/Stackup.h) passed to
// computeRatsnest/runDrc, a via just marks "these coincide" across every
// copper layer, the same way JunctionEntity does for schematic wires --
// the project's original, single-plane behavior. With a real stackup,
// throughHole (the default) still spans every layer in it -- a real
// through-hole via always does. Setting throughHole to false makes it a
// blind/buried via spanning only [fromLayer,toLayer] (order doesn't
// matter) of that stackup; fromLayer/toLayer are otherwise unused.
class ViaEntity : public Entity {
public:
    ViaEntity(EntityId id, LayerId layer, Point2D position, double diameter = 0.6, double drillDiameter = 0.3)
        : Entity(id, layer), m_position(position), m_diameter(diameter), m_drillDiameter(drillDiameter) {}

    const Point2D& position() const { return m_position; }
    double diameter() const { return m_diameter; }
    double drillDiameter() const { return m_drillDiameter; }

    bool throughHole = true;
    LayerId fromLayer = 0;
    LayerId toLayer = 0;

    EntityType type() const override { return EntityType::Via; }
    BoundingBox boundingBox() const override {
        BoundingBox box;
        box.expand(m_position - Point2D(m_diameter / 2, m_diameter / 2));
        box.expand(m_position + Point2D(m_diameter / 2, m_diameter / 2));
        return box;
    }
    double distanceTo(const Point2D& pt) const override {
        return std::max(0.0, pt.distanceTo(m_position) - m_diameter / 2.0);
    }
    void translate(const Point2D& delta) override { m_position = m_position + delta; }
    void rotate(const Point2D& center, double angleRadians) override {
        m_position = rotateAround(m_position, center, angleRadians);
    }
    void scale(const Point2D& center, double factor) override {
        m_position = scaleAround(m_position, center, factor);
        m_diameter *= factor;
        m_drillDiameter *= factor;
    }
    void mirror(const Point2D& a, const Point2D& b) override { m_position = mirrorAcross(m_position, a, b); }
    std::vector<Point2D> gripPoints() const override { return {m_position}; }
    void moveGripPoint(std::size_t index, const Point2D& newPos) override {
        if (index == 0) m_position = newPos;
    }
    std::vector<SnapPoint> snapCandidates() const override { return {{m_position, SnapKind::Center}}; }
    std::unique_ptr<Entity> clone() const override { return std::make_unique<ViaEntity>(*this); }

private:
    Point2D m_position;
    double m_diameter;
    double m_drillDiameter;
};

} // namespace lcad
