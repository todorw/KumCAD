#include "core/geometry/Polyline.h"

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

Point2D pointOnArc(const BulgeArc& arc, double angle) {
    return arc.center + Point2D(std::cos(angle), std::sin(angle)) * arc.radius;
}

// True if angle lies within the arc's signed sweep from startAngle.
bool angleInBulgeSweep(const BulgeArc& arc, double angle) {
    const double sweep = std::abs(arc.sweep);
    const double from = arc.sweep >= 0 ? arc.startAngle : arc.startAngle - sweep;
    double rel = std::fmod(angle - from, 2 * M_PI);
    if (rel < 0) rel += 2 * M_PI;
    return rel <= sweep + 1e-9;
}

double distanceToBulgeSegment(const Point2D& pt, const Point2D& a, const Point2D& b, double bulge) {
    const auto arc = bulgeToArc(a, b, bulge);
    if (!arc) return distanceToSegment(pt, a, b);
    const double angle = std::atan2(pt.y - arc->center.y, pt.x - arc->center.x);
    if (angleInBulgeSweep(*arc, angle)) {
        return std::abs(pt.distanceTo(arc->center) - arc->radius);
    }
    return std::min(pt.distanceTo(a), pt.distanceTo(b));
}

// Appends the points of the segment a->b (excluding a, including b),
// approximating arcs with chords short enough for display/measurement use.
void appendFlattenedSegment(std::vector<Point2D>& out, const Point2D& a, const Point2D& b, double bulge) {
    const auto arc = bulgeToArc(a, b, bulge);
    if (!arc) {
        out.push_back(b);
        return;
    }
    const int steps = std::max(2, static_cast<int>(std::ceil(std::abs(arc->sweep) / (M_PI / 16))));
    for (int i = 1; i < steps; ++i) {
        out.push_back(pointOnArc(*arc, arc->startAngle + arc->sweep * i / steps));
    }
    out.push_back(b);
}

} // namespace

std::optional<BulgeArc> bulgeToArc(const Point2D& a, const Point2D& b, double bulge) {
    if (std::abs(bulge) < 1e-9) return std::nullopt;
    const Point2D chord = b - a;
    const double chordLen = chord.length();
    if (chordLen < 1e-12) return std::nullopt;

    BulgeArc arc;
    arc.sweep = 4.0 * std::atan(bulge);
    arc.radius = (chordLen / 2.0) * (1.0 + bulge * bulge) / (2.0 * std::abs(bulge));
    // Center sits along the chord's left normal, on the far side of the chord
    // from the bulge for minor arcs and on the same side for major arcs.
    const Point2D leftNormal(-chord.y / chordLen, chord.x / chordLen);
    const double centerOffset = (chordLen / 2.0) * (1.0 - bulge * bulge) / (2.0 * bulge);
    arc.center = a + chord * 0.5 + leftNormal * centerOffset;
    arc.startAngle = std::atan2(a.y - arc.center.y, a.x - arc.center.x);
    return arc;
}

bool PolylineEntity::hasArcs() const {
    const std::size_t segments = m_closed ? m_bulges.size() : (m_bulges.empty() ? 0 : m_bulges.size() - 1);
    for (std::size_t i = 0; i < segments; ++i) {
        if (std::abs(m_bulges[i]) > 1e-9) return true;
    }
    return false;
}

void PolylineEntity::forEachSegment(const std::function<void(const Point2D&, const Point2D&, double)>& fn) const {
    for (std::size_t i = 0; i + 1 < m_vertices.size(); ++i) {
        fn(m_vertices[i], m_vertices[i + 1], m_bulges[i]);
    }
    if (m_closed && m_vertices.size() > 1) {
        fn(m_vertices.back(), m_vertices.front(), m_bulges.back());
    }
}

std::vector<Point2D> PolylineEntity::flattenedVertices() const {
    if (!hasArcs()) return m_vertices;
    std::vector<Point2D> out;
    if (m_vertices.empty()) return out;
    out.push_back(m_vertices.front());
    for (std::size_t i = 0; i + 1 < m_vertices.size(); ++i) {
        appendFlattenedSegment(out, m_vertices[i], m_vertices[i + 1], m_bulges[i]);
    }
    if (m_closed && m_vertices.size() > 1) {
        appendFlattenedSegment(out, m_vertices.back(), m_vertices.front(), m_bulges.back());
        out.pop_back(); // closing vertex duplicates the first
    }
    return out;
}

BoundingBox PolylineEntity::boundingBox() const {
    BoundingBox box;
    for (const auto& v : m_vertices) box.expand(v);
    // Arc segments can extend past their endpoints wherever the sweep crosses
    // a quadrant direction (0/90/180/270 degrees from the arc center).
    forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
        const auto arc = bulgeToArc(a, b, bulge);
        if (!arc) return;
        for (int q = 0; q < 4; ++q) {
            const double angle = q * M_PI / 2.0;
            if (angleInBulgeSweep(*arc, angle)) box.expand(pointOnArc(*arc, angle));
        }
    });
    return box;
}

double PolylineEntity::distanceTo(const Point2D& pt) const {
    double best = std::numeric_limits<double>::max();
    forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
        best = std::min(best, distanceToBulgeSegment(pt, a, b, bulge));
    });
    if (m_vertices.size() == 1) best = pt.distanceTo(m_vertices.front());
    return best;
}

void PolylineEntity::translate(const Point2D& delta) {
    for (auto& v : m_vertices) v = v + delta;
}

void PolylineEntity::rotate(const Point2D& center, double angleRadians) {
    for (auto& v : m_vertices) v = rotateAround(v, center, angleRadians);
}

void PolylineEntity::scale(const Point2D& center, double factor) {
    for (auto& v : m_vertices) v = scaleAround(v, center, factor);
}

void PolylineEntity::mirror(const Point2D& a, const Point2D& b) {
    for (auto& v : m_vertices) v = mirrorAcross(v, a, b);
    // Reflection reverses orientation, so every arc flips CW <-> CCW.
    for (auto& bulge : m_bulges) bulge = -bulge;
}

std::vector<Point2D> PolylineEntity::gripPoints() const {
    return m_vertices;
}

void PolylineEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index < m_vertices.size()) m_vertices[index] = newPos;
}

std::vector<SnapPoint> PolylineEntity::snapCandidates() const {
    std::vector<SnapPoint> result;
    for (const auto& v : m_vertices) result.push_back({v, SnapKind::Endpoint});
    forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
        if (const auto arc = bulgeToArc(a, b, bulge)) {
            result.push_back({pointOnArc(*arc, arc->startAngle + arc->sweep / 2.0), SnapKind::Midpoint});
            result.push_back({arc->center, SnapKind::Center});
        } else {
            result.push_back({a + (b - a) * 0.5, SnapKind::Midpoint});
        }
    });
    return result;
}

std::unique_ptr<Entity> PolylineEntity::clone() const {
    return std::make_unique<PolylineEntity>(*this);
}

} // namespace lcad
