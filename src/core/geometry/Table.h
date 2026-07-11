#pragma once

#include "core/geometry/Entity.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lcad {

// AutoCAD TABLE (simplified): a grid of cells anchored at a top-left
// insertion point, with independent row heights and column widths and plain
// text per cell (no per-cell formatting/fields/merges -- those are a much
// larger project than a first TABLE implementation warrants). Row 0 is the
// top row; column 0 is the leftmost column.
//
// rotate()/mirror() move the insertion point like any other point entity but
// leave the grid axis-aligned and the cell text unflipped -- the same
// unflipped-text behavior AutoCAD itself defaults to (MIRRTEXT off), just
// applied to the whole table rather than only its text.
class TableEntity : public Entity {
public:
    TableEntity(EntityId id, LayerId layer, Point2D position, std::vector<double> rowHeights,
                std::vector<double> colWidths, std::vector<std::string> cells, double textHeight = 2.5)
        : Entity(id, layer), m_position(position), m_rowHeights(std::move(rowHeights)),
          m_colWidths(std::move(colWidths)), m_cells(std::move(cells)), m_textHeight(textHeight) {
        m_cells.resize(rows() * cols());
    }

    const Point2D& position() const { return m_position; }
    int rows() const { return static_cast<int>(m_rowHeights.size()); }
    int cols() const { return static_cast<int>(m_colWidths.size()); }
    const std::vector<double>& rowHeights() const { return m_rowHeights; }
    const std::vector<double>& colWidths() const { return m_colWidths; }
    double textHeight() const { return m_textHeight; }

    double totalWidth() const;
    double totalHeight() const;

    const std::string& cellText(int row, int col) const;
    void setCellText(int row, int col, std::string text);

    // World-space rectangle of one cell.
    BoundingBox cellRect(int row, int col) const;
    // Row/column under pt, or nullopt if outside the grid.
    std::optional<std::pair<int, int>> cellAt(const Point2D& pt) const;

    EntityType type() const override { return EntityType::Table; }
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
    static const std::string s_empty;

    Point2D m_position; // top-left corner
    std::vector<double> m_rowHeights;
    std::vector<double> m_colWidths;
    std::vector<std::string> m_cells; // row-major, size rows() * cols()
    double m_textHeight;
};

} // namespace lcad
