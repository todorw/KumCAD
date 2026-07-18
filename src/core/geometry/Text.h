#pragma once

#include "core/geometry/Entity.h"

#include <string>

namespace lcad {

// Single-line text: insertion point (baseline-left, matching AutoCAD's
// default justification), content, height in world units, and rotation in
// radians. core has no Qt dependency, so boundingBox()/distanceTo() use a
// simple monospace-ish width heuristic (0.6 * height per character) rather
// than real font metrics -- fine for hit-testing, not for exact typography.
// The actual on-screen rendering (DrawingView) uses a real QFont.
class TextEntity : public Entity {
public:
    TextEntity(EntityId id, LayerId layer, Point2D position, std::string text, double height,
               double rotationRadians = 0.0)
        : Entity(id, layer), m_position(position), m_text(std::move(text)), m_height(height),
          m_rotation(rotationRadians) {}

    const Point2D& position() const { return m_position; }
    const std::string& text() const { return m_text; }
    void setText(std::string text) { m_text = std::move(text); }

    // Real AutoCAD FIELD capability (see core/document/Fields.h): when
    // non-empty, this is the ORIGINAL {{...}}-placeholder text UPDATEFIELD
    // re-evaluates -- text() itself always holds the last-resolved
    // (currently displayed) value, matching how a real field stays live
    // but only actually updates on specific triggers, not continuously.
    const std::string& fieldTemplate() const { return m_fieldTemplate; }
    void setFieldTemplate(std::string tmpl) { m_fieldTemplate = std::move(tmpl); }
    double height() const { return m_height; }
    double rotation() const { return m_rotation; }

    // Named text style (STYLE table) resolved at render time; unknown names
    // fall back to the default face.
    const std::string& styleName() const { return m_styleName; }
    void setStyleName(std::string name) { m_styleName = std::move(name); }

    // Per-entity horizontal stretch (real DXF group code 41, "Relative
    // X scale factor") -- separate from the STYLE table's own width
    // factor, matching real AutoCAD (TEXTFIT sets this per-object
    // override, not the whole style). 1.0 = no stretch.
    double widthFactor() const { return m_widthFactor; }
    void setWidthFactor(double factor) { m_widthFactor = factor; }

    double approximateWidth() const;

    EntityType type() const override { return EntityType::Text; }
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
    Point2D m_position;
    std::string m_text;
    double m_height;
    double m_rotation;
    std::string m_styleName = "Standard";
    std::string m_fieldTemplate;
    double m_widthFactor = 1.0;
};

} // namespace lcad
