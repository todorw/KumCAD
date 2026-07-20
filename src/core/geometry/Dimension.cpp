#include "core/geometry/Dimension.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

std::string formatValue(double value, int decimals, const char* prefix = "", const char* suffix = "") {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s%.*f%s", prefix, decimals, value, suffix);
    return buffer;
}

double normalizeAngle(double a) {
    a = std::fmod(a, 2 * M_PI);
    if (a < 0) a += 2 * M_PI;
    return a;
}

// Keep dimension labels upright-readable, matching AutoCAD's convention.
double readableAngle(double a) {
    if (a > M_PI / 2 + 1e-9 || a < -M_PI / 2 + 1e-9) a += M_PI;
    return a;
}

} // namespace

DimensionEntity::Geometry DimensionEntity::geometry() const {
    Geometry geo;

    switch (m_kind) {
    case DimensionKind::Aligned: {
        const Point2D span = m_p2 - m_p1;
        const double len = span.length();
        const Point2D dir = len > 1e-12 ? span * (1.0 / len) : Point2D(1, 0);
        const Point2D normal(-dir.y, dir.x);
        const double offset = (m_linePoint - m_p1).dot(normal);
        geo.dimA = m_p1 + normal * offset;
        geo.dimB = m_p2 + normal * offset;
        geo.value = len;
        geo.textAngle = readableAngle(std::atan2(dir.y, dir.x));
        geo.label = formatValue(geo.value, m_decimals);
        geo.ext1A = m_p1;
        geo.ext1B = geo.dimA;
        geo.ext2A = m_p2;
        geo.ext2B = geo.dimB;
        break;
    }
    case DimensionKind::Linear: {
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
        geo.textAngle = readableAngle(geo.textAngle);
        geo.label = formatValue(geo.value, m_decimals);
        geo.ext1A = m_p1;
        geo.ext1B = geo.dimA;
        geo.ext2A = m_p2;
        geo.ext2B = geo.dimB;
        break;
    }
    case DimensionKind::Radius:
    case DimensionKind::Diameter: {
        const Point2D span = m_p2 - m_p1;
        const double radius = span.length();
        const Point2D dir = radius > 1e-12 ? span * (1.0 / radius) : Point2D(1, 0);
        const bool diameter = m_kind == DimensionKind::Diameter;
        // Radius: line from the center to the curve point. Diameter: the full
        // chord through the center. Single arrow at the curve for radius.
        geo.dimA = diameter ? m_p1 - dir * radius : m_p1;
        geo.dimB = m_p2;
        geo.arrow1 = diameter;
        geo.value = diameter ? 2.0 * radius : radius;
        geo.label = formatValue(geo.value, m_decimals, diameter ? "\xC3\x98" : "R"); // U+00D8 diameter sign
        geo.textAngle = readableAngle(std::atan2(dir.y, dir.x));
        // Label sits at the user-dragged point projected onto the ray.
        const double along = (m_linePoint - m_p1).dot(dir);
        geo.textPos = m_p1 + dir * along;
        // No extension lines.
        geo.ext1A = geo.ext1B = geo.dimA;
        geo.ext2A = geo.ext2B = geo.dimB;
        return geo;
    }
    case DimensionKind::Angular: {
        const Point2D r1 = m_p1 - m_vertex;
        const Point2D r2 = m_p2 - m_vertex;
        const double a1 = std::atan2(r1.y, r1.x);
        const double a2 = std::atan2(r2.y, r2.x);
        const double radius = std::max(m_linePoint.distanceTo(m_vertex), 1e-6);
        // The measured side is the sweep the arc-position point falls in.
        const double aL = std::atan2(m_linePoint.y - m_vertex.y, m_linePoint.x - m_vertex.x);
        double start = a1;
        double sweep = normalizeAngle(a2 - a1);
        if (normalizeAngle(aL - a1) > sweep) {
            start = a2;
            sweep = 2 * M_PI - sweep;
        }
        geo.angular = true;
        geo.arcCenter = m_vertex;
        geo.arcRadius = radius;
        geo.arcStartAngle = start;
        geo.arcEndAngle = start + sweep;
        geo.value = sweep * 180.0 / M_PI;
        geo.label = formatValue(geo.value, std::max(0, m_decimals - 1), "", "\xC2\xB0");
        geo.dimA = m_vertex + Point2D(std::cos(start), std::sin(start)) * radius;
        geo.dimB = m_vertex + Point2D(std::cos(start + sweep), std::sin(start + sweep)) * radius;
        // Extension lines from the picked ray points out to the arc.
        auto extEnd = [&](double angle) { return m_vertex + Point2D(std::cos(angle), std::sin(angle)) * radius; };
        geo.ext1A = m_p1;
        geo.ext1B = extEnd(a1);
        geo.ext2A = m_p2;
        geo.ext2B = extEnd(a2);
        const double midAngle = start + sweep / 2.0;
        geo.textPos = m_vertex + Point2D(std::cos(midAngle), std::sin(midAngle)) * (radius + 0.9 * m_textHeight);
        geo.textAngle = readableAngle(midAngle - M_PI / 2); // tangent direction
        return geo;
    }
    case DimensionKind::Ordinate: {
        // Real AutoCAD's own rule: a leader drawn mostly VERTICALLY from
        // the feature point measures the X ordinate ("Xdatum"); mostly
        // HORIZONTALLY measures Y ("Ydatum") -- auto-detected from the
        // leader's own end point rather than a separately stored flag.
        const Point2D leader = m_linePoint - m_p1;
        const bool xType = std::abs(leader.x) <= std::abs(leader.y);
        geo.value = xType ? (m_p1.x - m_vertex.x) : (m_p1.y - m_vertex.y);
        geo.label = formatValue(geo.value, m_decimals);
        // A real ordinate leader jogs to align with the label instead of
        // running diagonally: straight along the measured axis first,
        // then over to linePoint.
        const Point2D jog = xType ? Point2D(m_p1.x, m_linePoint.y) : Point2D(m_linePoint.x, m_p1.y);
        geo.ext1A = m_p1;
        geo.ext1B = jog;
        geo.dimA = jog;
        geo.dimB = m_linePoint;
        geo.ext2A = geo.ext2B = jog; // no second extension line needed
        geo.arrow1 = false;
        geo.arrow2 = false;
        geo.textPos = m_linePoint;
        geo.textAngle = 0.0; // ordinate labels stay horizontal regardless of measurement axis
        return geo;
    }
    case DimensionKind::Jogged: {
        // The measured value comes from the TRUE center/curve point
        // (p1/p2); the drawn line instead runs from the override center
        // (m_vertex, picked because the true center is off-page/
        // inconvenient -- the entire reason this kind exists) to the
        // label, with a jog (zigzag) partway marked via jogPoint below
        // for the renderer to draw that symbol.
        geo.value = m_p1.distanceTo(m_p2);
        geo.label = formatValue(geo.value, m_decimals, "R");
        geo.dimA = m_vertex;
        geo.dimB = m_linePoint;
        geo.arrow1 = true;
        geo.arrow2 = false;
        geo.jogged = true;
        const Point2D lineSpan = geo.dimB - geo.dimA;
        const double lineLen = lineSpan.length();
        const Point2D lineDir = lineLen > 1e-12 ? lineSpan * (1.0 / lineLen) : Point2D(1, 0);
        const Point2D lineNormal(-lineDir.y, lineDir.x);
        geo.jogPoint = geo.dimA + lineDir * (lineLen * 0.5) + lineNormal * (0.75 * m_textHeight);
        geo.ext1A = geo.ext1B = geo.dimA;
        geo.ext2A = geo.ext2B = geo.dimB;
        geo.textPos = m_linePoint;
        geo.textAngle = readableAngle(std::atan2(lineDir.y, lineDir.x));
        return geo;
    }
    case DimensionKind::ArcLength: {
        // Same construction as Angular, except the dimension arc is
        // drawn at the MEASURED arc's own real radius (p1's distance
        // from vertex) rather than an arbitrary user-dragged one --
        // an arc-length dimension measures that one real arc, not an
        // abstract angle at a chosen radius.
        const Point2D r1 = m_p1 - m_vertex;
        const Point2D r2 = m_p2 - m_vertex;
        const double a1 = std::atan2(r1.y, r1.x);
        const double a2 = std::atan2(r2.y, r2.x);
        const double radius = r1.length();
        const double aL = std::atan2(m_linePoint.y - m_vertex.y, m_linePoint.x - m_vertex.x);
        double start = a1;
        double sweep = normalizeAngle(a2 - a1);
        if (normalizeAngle(aL - a1) > sweep) {
            start = a2;
            sweep = 2 * M_PI - sweep;
        }
        geo.angular = true;
        geo.arcCenter = m_vertex;
        geo.arcRadius = radius;
        geo.arcStartAngle = start;
        geo.arcEndAngle = start + sweep;
        geo.value = radius * sweep; // arc length = radius * angle (radians)
        geo.label = formatValue(geo.value, m_decimals, "\xE2\x8C\x92"); // U+2312 ARC symbol, AutoCAD's own arc-length prefix
        geo.dimA = m_vertex + Point2D(std::cos(start), std::sin(start)) * radius;
        geo.dimB = m_vertex + Point2D(std::cos(start + sweep), std::sin(start + sweep)) * radius;
        geo.ext1A = m_p1;
        geo.ext1B = geo.dimA;
        geo.ext2A = m_p2;
        geo.ext2B = geo.dimB;
        const double midAngle = start + sweep / 2.0;
        geo.textPos = m_vertex + Point2D(std::cos(midAngle), std::sin(midAngle)) * (radius + 0.9 * m_textHeight);
        geo.textAngle = readableAngle(midAngle - M_PI / 2);
        return geo;
    }
    }

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
    if (geo.angular) {
        box.expand(geo.arcCenter + Point2D(geo.arcRadius, 0));
        box.expand(geo.arcCenter - Point2D(geo.arcRadius, 0));
        box.expand(geo.arcCenter + Point2D(0, geo.arcRadius));
        box.expand(geo.arcCenter - Point2D(0, geo.arcRadius));
    }
    if (geo.jogged) box.expand(geo.jogPoint);
    if (m_kind == DimensionKind::Ordinate || m_kind == DimensionKind::Jogged || m_kind == DimensionKind::ArcLength) {
        box.expand(m_vertex);
    }
    return box;
}

double DimensionEntity::distanceTo(const Point2D& pt) const {
    const Geometry geo = geometry();
    double best = std::numeric_limits<double>::max();
    if (geo.angular) {
        // Distance to the arc within its sweep.
        const double angle = std::atan2(pt.y - geo.arcCenter.y, pt.x - geo.arcCenter.x);
        const double sweep = geo.arcEndAngle - geo.arcStartAngle;
        double rel = normalizeAngle(angle - geo.arcStartAngle);
        if (rel <= sweep + 1e-9) {
            best = std::abs(pt.distanceTo(geo.arcCenter) - geo.arcRadius);
        } else {
            best = std::min(pt.distanceTo(geo.dimA), pt.distanceTo(geo.dimB));
        }
    } else {
        best = std::min(best, distanceToSegment(pt, geo.dimA, geo.dimB));
    }
    best = std::min(best, distanceToSegment(pt, geo.ext1A, geo.ext1B));
    best = std::min(best, distanceToSegment(pt, geo.ext2A, geo.ext2B));
    return best;
}

void DimensionEntity::translate(const Point2D& delta) {
    m_p1 = m_p1 + delta;
    m_p2 = m_p2 + delta;
    m_linePoint = m_linePoint + delta;
    m_vertex = m_vertex + delta;
}

void DimensionEntity::rotate(const Point2D& center, double angleRadians) {
    m_p1 = rotateAround(m_p1, center, angleRadians);
    m_p2 = rotateAround(m_p2, center, angleRadians);
    m_linePoint = rotateAround(m_linePoint, center, angleRadians);
    m_vertex = rotateAround(m_vertex, center, angleRadians);
}

void DimensionEntity::scale(const Point2D& center, double factor) {
    m_p1 = scaleAround(m_p1, center, factor);
    m_p2 = scaleAround(m_p2, center, factor);
    m_linePoint = scaleAround(m_linePoint, center, factor);
    m_vertex = scaleAround(m_vertex, center, factor);
    m_textHeight *= factor;
    m_arrowSize *= factor;
}

void DimensionEntity::mirror(const Point2D& a, const Point2D& b) {
    m_p1 = mirrorAcross(m_p1, a, b);
    m_p2 = mirrorAcross(m_p2, a, b);
    m_linePoint = mirrorAcross(m_linePoint, a, b);
    m_vertex = mirrorAcross(m_vertex, a, b);
}

namespace {
bool usesVertexGrip(DimensionKind kind) {
    return kind == DimensionKind::Angular || kind == DimensionKind::Ordinate || kind == DimensionKind::Jogged ||
          kind == DimensionKind::ArcLength;
}
} // namespace

std::vector<Point2D> DimensionEntity::gripPoints() const {
    if (usesVertexGrip(m_kind)) return {m_p1, m_p2, m_linePoint, m_vertex};
    return {m_p1, m_p2, m_linePoint};
}

void DimensionEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) m_p1 = newPos;
    else if (index == 1) m_p2 = newPos;
    else if (index == 2) m_linePoint = newPos;
    else if (index == 3 && usesVertexGrip(m_kind)) m_vertex = newPos;
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
