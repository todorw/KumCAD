#include "core/geometry/Table.h"

#include <algorithm>
#include <limits>
#include <numeric>

namespace lcad {

const std::string TableEntity::s_empty;

namespace {

double distanceToSegment(const Point2D& pt, const Point2D& a, const Point2D& b) {
    const Point2D seg = b - a;
    const double lenSq = seg.dot(seg);
    if (lenSq < 1e-12) return pt.distanceTo(a);
    double t = (pt - a).dot(seg) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return pt.distanceTo(a + seg * t);
}

} // namespace

double TableEntity::totalWidth() const { return std::accumulate(m_colWidths.begin(), m_colWidths.end(), 0.0); }
double TableEntity::totalHeight() const { return std::accumulate(m_rowHeights.begin(), m_rowHeights.end(), 0.0); }

const std::string& TableEntity::cellText(int row, int col) const {
    if (row < 0 || col < 0 || row >= rows() || col >= cols()) return s_empty;
    return m_cells[static_cast<std::size_t>(row) * cols() + col];
}

void TableEntity::setCellText(int row, int col, std::string text) {
    if (row < 0 || col < 0 || row >= rows() || col >= cols()) return;
    m_cells[static_cast<std::size_t>(row) * cols() + col] = std::move(text);
}

BoundingBox TableEntity::cellRect(int row, int col) const {
    BoundingBox box;
    if (row < 0 || col < 0 || row >= rows() || col >= cols()) return box;
    double x0 = m_position.x;
    for (int c = 0; c < col; ++c) x0 += m_colWidths[c];
    double y0 = m_position.y;
    for (int r = 0; r < row; ++r) y0 -= m_rowHeights[r];
    box.expand(Point2D(x0, y0));
    box.expand(Point2D(x0 + m_colWidths[col], y0 - m_rowHeights[row]));
    return box;
}

std::optional<std::pair<int, int>> TableEntity::cellAt(const Point2D& pt) const {
    if (pt.x < m_position.x || pt.y > m_position.y) return std::nullopt;
    double x = m_position.x;
    int col = -1;
    for (int c = 0; c < cols(); ++c) {
        if (pt.x >= x && pt.x <= x + m_colWidths[c]) {
            col = c;
            break;
        }
        x += m_colWidths[c];
    }
    double y = m_position.y;
    int row = -1;
    for (int r = 0; r < rows(); ++r) {
        if (pt.y <= y && pt.y >= y - m_rowHeights[r]) {
            row = r;
            break;
        }
        y -= m_rowHeights[r];
    }
    if (row < 0 || col < 0) return std::nullopt;
    return std::make_pair(row, col);
}

BoundingBox TableEntity::boundingBox() const {
    BoundingBox box;
    box.expand(m_position);
    box.expand(Point2D(m_position.x + totalWidth(), m_position.y - totalHeight()));
    return box;
}

double TableEntity::distanceTo(const Point2D& pt) const {
    const BoundingBox box = boundingBox();
    if (box.contains(pt)) return 0.0;
    const Point2D tl(box.min.x, box.max.y);
    const Point2D tr(box.max.x, box.max.y);
    const Point2D bl(box.min.x, box.min.y);
    const Point2D br(box.max.x, box.min.y);
    double best = std::numeric_limits<double>::max();
    best = std::min(best, distanceToSegment(pt, tl, tr));
    best = std::min(best, distanceToSegment(pt, tr, br));
    best = std::min(best, distanceToSegment(pt, br, bl));
    best = std::min(best, distanceToSegment(pt, bl, tl));
    return best;
}

void TableEntity::translate(const Point2D& delta) { m_position = m_position + delta; }

void TableEntity::rotate(const Point2D& center, double angleRadians) {
    m_position = rotateAround(m_position, center, angleRadians);
}

void TableEntity::scale(const Point2D& center, double factor) {
    m_position = scaleAround(m_position, center, factor);
    for (double& h : m_rowHeights) h *= factor;
    for (double& w : m_colWidths) w *= factor;
    m_textHeight *= factor;
}

void TableEntity::mirror(const Point2D& a, const Point2D& b) { m_position = mirrorAcross(m_position, a, b); }

std::vector<Point2D> TableEntity::gripPoints() const {
    const BoundingBox box = boundingBox();
    return {Point2D(box.min.x, box.max.y), Point2D(box.max.x, box.max.y), Point2D(box.min.x, box.min.y),
            Point2D(box.max.x, box.min.y)};
}

void TableEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    const double oldWidth = totalWidth();
    const double oldHeight = totalHeight();
    switch (index) {
    case 0: // top-left: reposition, keep size
        m_position = newPos;
        break;
    case 1: { // top-right: resize width only
        const double newWidth = std::max(0.1, newPos.x - m_position.x);
        const double ratio = oldWidth > 1e-9 ? newWidth / oldWidth : 1.0;
        for (double& w : m_colWidths) w *= ratio;
        break;
    }
    case 2: { // bottom-left: resize height only
        const double newHeight = std::max(0.1, m_position.y - newPos.y);
        const double ratio = oldHeight > 1e-9 ? newHeight / oldHeight : 1.0;
        for (double& h : m_rowHeights) h *= ratio;
        break;
    }
    case 3: { // bottom-right: resize both
        const double newWidth = std::max(0.1, newPos.x - m_position.x);
        const double newHeight = std::max(0.1, m_position.y - newPos.y);
        const double wRatio = oldWidth > 1e-9 ? newWidth / oldWidth : 1.0;
        const double hRatio = oldHeight > 1e-9 ? newHeight / oldHeight : 1.0;
        for (double& w : m_colWidths) w *= wRatio;
        for (double& h : m_rowHeights) h *= hRatio;
        break;
    }
    default:
        break;
    }
}

std::vector<SnapPoint> TableEntity::snapCandidates() const {
    std::vector<SnapPoint> result;
    std::vector<double> xs{m_position.x};
    for (double w : m_colWidths) xs.push_back(xs.back() + w);
    std::vector<double> ys{m_position.y};
    for (double h : m_rowHeights) ys.push_back(ys.back() - h);
    for (double x : xs) {
        for (double y : ys) result.push_back({Point2D(x, y), SnapKind::Endpoint});
    }
    return result;
}

std::unique_ptr<Entity> TableEntity::clone() const { return std::make_unique<TableEntity>(*this); }

} // namespace lcad
