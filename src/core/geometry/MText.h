#pragma once

#include "core/geometry/Entity.h"

#include <string>
#include <vector>

namespace lcad {

// DXF/DWG MTEXT content codes <-> plain text: \P becomes a newline, \~ a
// space, escaped characters are unescaped, and inline formatting codes
// (\f...; \H...; {\...} etc.) are stripped. encode does the reverse minimal
// escaping for writing.
std::string decodeMTextContent(const std::string& raw);
std::string encodeMTextContent(const std::string& text);

// Multiline text (AutoCAD MTEXT): top-left insertion point, newline-separated
// content, per-line character height, a wrapping width (0 = never wrap), and
// rotation. Like TextEntity, core approximates glyph metrics (0.6 * height
// per character, 1.6 * height line advance); rendering uses real fonts.
class MTextEntity : public Entity {
public:
    MTextEntity(EntityId id, LayerId layer, Point2D position, std::string text, double height, double width = 0.0,
                double rotationRadians = 0.0)
        : Entity(id, layer), m_position(position), m_text(std::move(text)), m_height(height), m_width(width),
          m_rotation(rotationRadians) {}

    const Point2D& position() const { return m_position; }
    const std::string& text() const { return m_text; }
    double height() const { return m_height; }
    double width() const { return m_width; }
    double rotation() const { return m_rotation; }
    void setText(std::string text) { m_text = std::move(text); }

    // Named text style (STYLE table) resolved at render time.
    const std::string& styleName() const { return m_styleName; }
    void setStyleName(std::string name) { m_styleName = std::move(name); }

    double lineAdvance() const { return 1.6 * m_height; }

    // Content split on newlines and word-wrapped to width() using the
    // approximate glyph metric -- shared by hit-testing and the renderer so
    // what you click matches what you see.
    std::vector<std::string> wrappedLines() const;

    // Extents of the wrapped block, in unrotated local units.
    double blockWidth() const;
    double blockHeight() const;

    EntityType type() const override { return EntityType::MText; }
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
    Point2D m_position; // top-left corner of the text block
    std::string m_text;
    double m_height;
    double m_width;
    double m_rotation;
    std::string m_styleName = "Standard";
};

} // namespace lcad
