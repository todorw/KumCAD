#pragma once

#include "core/geometry/Entity.h"

#include <algorithm>
#include <string>

namespace lcad {

// AutoCAD ATTDEF: an attribute definition living inside a block definition.
// When the block is inserted, each ATTDEF prompts for a value; the values
// live on the InsertEntity and render as text at the definition's position
// transformed by the insert. Standalone (outside a block) it draws its tag,
// like AutoCAD does.
class AttDefEntity : public Entity {
public:
    AttDefEntity(EntityId id, LayerId layer, Point2D position, std::string tag, std::string prompt,
                 std::string defaultValue, double height, double rotationRadians = 0.0)
        : Entity(id, layer), m_position(position), m_tag(std::move(tag)), m_prompt(std::move(prompt)),
          m_defaultValue(std::move(defaultValue)), m_height(height), m_rotation(rotationRadians) {}

    const Point2D& position() const { return m_position; }
    const std::string& tag() const { return m_tag; }
    const std::string& prompt() const { return m_prompt; }
    const std::string& defaultValue() const { return m_defaultValue; }
    double height() const { return m_height; }
    double rotation() const { return m_rotation; }

    EntityType type() const override { return EntityType::AttDef; }
    BoundingBox boundingBox() const override {
        // Same monospace-ish heuristic as TextEntity, over the tag.
        BoundingBox box;
        box.expand(m_position);
        box.expand(m_position + rotateAround(Point2D(0.6 * m_height * m_tag.size(), m_height), Point2D(), m_rotation));
        return box;
    }
    double distanceTo(const Point2D& pt) const override {
        const BoundingBox box = boundingBox();
        const Point2D clamped(std::clamp(pt.x, box.min.x, box.max.x), std::clamp(pt.y, box.min.y, box.max.y));
        return pt.distanceTo(clamped);
    }
    void translate(const Point2D& delta) override { m_position = m_position + delta; }
    void rotate(const Point2D& center, double angleRadians) override {
        m_position = rotateAround(m_position, center, angleRadians);
        m_rotation += angleRadians;
    }
    void scale(const Point2D& center, double factor) override {
        m_position = scaleAround(m_position, center, factor);
        m_height *= factor;
    }
    void mirror(const Point2D& a, const Point2D& b) override { m_position = mirrorAcross(m_position, a, b); }
    std::vector<Point2D> gripPoints() const override { return {m_position}; }
    void moveGripPoint(std::size_t index, const Point2D& newPos) override {
        if (index == 0) m_position = newPos;
    }
    std::vector<SnapPoint> snapCandidates() const override { return {{m_position, SnapKind::Endpoint}}; }
    std::unique_ptr<Entity> clone() const override { return std::make_unique<AttDefEntity>(*this); }

private:
    Point2D m_position;
    std::string m_tag;
    std::string m_prompt;
    std::string m_defaultValue;
    double m_height;
    double m_rotation;
};

} // namespace lcad
