#pragma once

#include "core/document/Block.h"
#include "core/geometry/Entity.h"

#include <string>
#include <utility>
#include <vector>

namespace lcad {

// A placed reference to a BlockDefinition: position, uniform scale, and
// rotation. The definition is owned by the Document and outlives the insert.
class InsertEntity : public Entity {
public:
    InsertEntity(EntityId id, LayerId layer, const BlockDefinition* block, Point2D position, double scaleFactor = 1.0,
                 double rotationRadians = 0.0)
        : Entity(id, layer), m_block(block), m_position(position), m_scale(scaleFactor),
          m_rotation(rotationRadians) {}

    const BlockDefinition* block() const { return m_block; }
    const std::string& blockName() const { return m_block->name; }

    // Attribute values keyed by the block's ATTDEF tags. instantiate()
    // materializes them as text at the definitions' transformed positions,
    // so rendering, hit-testing, and EXPLODE all see them.
    const std::vector<std::pair<std::string, std::string>>& attributes() const { return m_attributes; }
    void setAttribute(const std::string& tag, const std::string& value);
    const std::string* attributeValue(const std::string& tag) const;
    const Point2D& position() const { return m_position; }
    double scaleFactor() const { return m_scale; }
    double rotation() const { return m_rotation; }

    // The block's children transformed into world space (scale, then rotate,
    // then translate) -- what rendering, hit-testing, and explode all share.
    std::vector<std::unique_ptr<Entity>> instantiate() const;

    EntityType type() const override { return EntityType::Insert; }
    BoundingBox boundingBox() const override;
    double distanceTo(const Point2D& pt) const override;
    void translate(const Point2D& delta) override;
    void rotate(const Point2D& center, double angleRadians) override;
    void scale(const Point2D& center, double factor) override;
    // Approximated as a reflected rotation; a true mirror needs negative
    // scale support, so asymmetric blocks will differ from AutoCAD here.
    void mirror(const Point2D& a, const Point2D& b) override;
    std::vector<Point2D> gripPoints() const override;
    void moveGripPoint(std::size_t index, const Point2D& newPos) override;
    std::vector<SnapPoint> snapCandidates() const override;
    std::unique_ptr<Entity> clone() const override;

private:
    const BlockDefinition* m_block;
    Point2D m_position;
    double m_scale;
    double m_rotation;
    std::vector<std::pair<std::string, std::string>> m_attributes;
};

} // namespace lcad
