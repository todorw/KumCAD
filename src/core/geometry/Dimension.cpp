#include "core/geometry/Dimension.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lcad {

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

DimensionEntity::Geometry DimensionEntity::geometry() const {
    Geometry geo;

    if (m_aligned) {
        const Point2D span = m_p2 - m_p1;
        const double len = span.length();
        const Point2D dir = len > 1e-12 ? span * (1.0 / len) : Point2D(1, 0);
        const Point2D normal(-dir.y, dir.x);
        const double offset = (m_linePoint - m_p1).dot(normal);
        geo.dimA = m_p1 + normal * offset;
        geo.dimB = m_p2 + normal * offset;
        geo.value = len;
        geo.textAngle = std::atan2(dir.y, dir.x);
    } else {
        // Pick horizontal vs vertical by where the line point was dragged
        // relative to the measured span, like DIMLINEAR does.
        const Point2D mid = (m_p1 + m_p2) * 0.5;
        const bool horizontal = std::abs(m_linePoint.y - mid.y) >= std::abs(m_linePoint.x - mid.x);
        if (horizontal) {
            geo.dimA = Point2D(m_p1.x, m_linePoint.y);
            geo.dimB = Point2D(m_p2.x, m_linePoint.y);
            geo.value = std::abs(m_p2.x - m_p1.x);
            geo.textAngle = 0.0;
        } else {
            geo.dimA = Point2D(m_linePoint.x, m_p1.y);
            geo.dimB = Point2D(m_linePoint.x, m_p2.y);
            geo.value = std::abs(m_p2.y - m_p1.y);
            geo.textAngle = M_PI / 2;
        }
    }

    // Keep the label upright-readable, matching AutoCAD's convention.
    if (geo.textAngle > M_PI / 2 + 1e-9 || geo.textAngle < -M_PI / 2 + 1e-9) geo.textAngle += M_PI;

    geo.ext1A = m_p1;
    geo.ext1B = geo.dimA;
    geo.ext2A = m_p2;
    geo.ext2B = geo.dimB;

    const Point2D dimSpan = geo.dimB - geo.dimA;
    const double dimLen = dimSpan.length();
    const Point2D dimDir = dimLen > 1e-12 ? dimSpan * (1.0 / dimLen) : Point2D(1, 0);
    const Point2D dimNormal(-dimDir.y, dimDir.x);
    geo.textPos = (geo.dimA + geo.dimB) * 0.5 + dimNormal * (0.7 * m_textHeight);
    return geo;
}

BoundingBox DimensionEntity::boundingBox() const {
    const Geometry geo = geometry();
    BoundingBox box;
    box.expand(m_p1);
    box.expand(m_p2);
    box.expand(geo.dimA);
    box.expand(geo.dimB);
    box.expand(geo.textPos);
    return box;
}

double DimensionEntity::distanceTo(const Point2D& pt) const {
    const Geometry geo = geometry();
    double best = std::numeric_limits<double>::max();
    best = std::min(best, distanceToSegment(pt, geo.dimA, geo.dimB));
    best = std::min(best, distanceToSegment(pt, geo.ext1A, geo.ext1B));
    best = std::min(best, distanceToSegment(pt, geo.ext2A, geo.ext2B));
    return best;
}

void DimensionEntity::translate(const Point2D& delta) {
    m_p1 = m_p1 + delta;
    m_p2 = m_p2 + delta;
    m_linePoint = m_linePoint + delta;
}

void DimensionEntity::rotate(const Point2D& center, double angleRadians) {
    m_p1 = rotateAround(m_p1, center, angleRadians);
    m_p2 = rotateAround(m_p2, center, angleRadians);
    m_linePoint = rotateAround(m_linePoint, center, angleRadians);
}

void DimensionEntity::scale(const Point2D& center, double factor) {
    m_p1 = scaleAround(m_p1, center, factor);
    m_p2 = scaleAround(m_p2, center, factor);
    m_linePoint = scaleAround(m_linePoint, center, factor);
    m_textHeight *= factor;
}

void DimensionEntity::mirror(const Point2D& a, const Point2D& b) {
    m_p1 = mirrorAcross(m_p1, a, b);
    m_p2 = mirrorAcross(m_p2, a, b);
    m_linePoint = mirrorAcross(m_linePoint, a, b);
}

std::vector<Point2D> DimensionEntity::gripPoints() const {
    return {m_p1, m_p2, m_linePoint};
}

void DimensionEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) m_p1 = newPos;
    else if (index == 1) m_p2 = newPos;
    else if (index == 2) m_linePoint = newPos;
}

std::vector<SnapPoint> DimensionEntity::snapCandidates() const {
    return {
        {m_p1, SnapKind::Endpoint},
        {m_p2, SnapKind::Endpoint},
    };
}

std::unique_ptr<Entity> DimensionEntity::clone() const {
    return std::make_unique<DimensionEntity>(*this);
}

} // namespace lcad
