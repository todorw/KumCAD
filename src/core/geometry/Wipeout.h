#pragma once

#include "core/geometry/Entity.h"

#include <vector>

namespace lcad {

// AutoCAD's WIPEOUT: a closed polygon that masks whatever was drawn
// before it (draw order == this codebase's entity list order, so a
// freshly-created wipeout already draws on top, matching real AutoCAD's
// default behavior without needing a separate DRAWORDER concept) by
// filling itself with the canvas background color instead of a layer/
// entity color. showFrame toggles the boundary outline (real AutoCAD's
// WIPEOUTFRAME is a document-wide system variable; kept per-entity here,
// a real, disclosed simplification -- a document with many wipeouts
// can't hide every frame in one toggle the way AutoCAD's can).
class WipeoutEntity : public Entity {
public:
    WipeoutEntity(EntityId id, LayerId layer, std::vector<Point2D> vertices, bool showFrame = true)
        : Entity(id, layer), m_vertices(std::move(vertices)), m_showFrame(showFrame) {}

    const std::vector<Point2D>& vertices() const { return m_vertices; }
    bool showFrame() const { return m_showFrame; }
    void setShowFrame(bool show) { m_showFrame = show; }

    bool containsPoint(const Point2D& pt) const;

    EntityType type() const override { return EntityType::Wipeout; }
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
    bool m_showFrame;
};

} // namespace lcad
