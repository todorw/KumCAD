#include "core/geometry/MLeader.h"

#include <algorithm>
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

// Every leg's points followed by the shared landing, in order -- what the
// leg actually draws as a polyline.
std::vector<Point2D> legChain(const std::vector<Point2D>& leg, const Point2D& landing) {
    std::vector<Point2D> chain = leg;
    chain.push_back(landing);
    return chain;
}

} // namespace

BoundingBox MLeaderEntity::boundingBox() const {
    BoundingBox box;
    box.expand(m_landing);
    for (const auto& leg : m_legs) {
        for (const Point2D& p : leg) box.expand(p);
    }
    return box;
}

double MLeaderEntity::distanceTo(const Point2D& pt) const {
    double best = std::numeric_limits<double>::max();
    for (const auto& leg : m_legs) {
        const std::vector<Point2D> chain = legChain(leg, m_landing);
        for (std::size_t i = 0; i + 1 < chain.size(); ++i) best = std::min(best, distanceToSegment(pt, chain[i], chain[i + 1]));
    }
    return best;
}

void MLeaderEntity::translate(const Point2D& delta) {
    m_landing = m_landing + delta;
    for (auto& leg : m_legs) {
        for (auto& p : leg) p = p + delta;
    }
}

void MLeaderEntity::rotate(const Point2D& center, double angleRadians) {
    m_landing = rotateAround(m_landing, center, angleRadians);
    for (auto& leg : m_legs) {
        for (auto& p : leg) p = rotateAround(p, center, angleRadians);
    }
}

void MLeaderEntity::scale(const Point2D& center, double factor) {
    m_landing = scaleAround(m_landing, center, factor);
    for (auto& leg : m_legs) {
        for (auto& p : leg) p = scaleAround(p, center, factor);
    }
    m_arrowSize *= factor;
}

void MLeaderEntity::mirror(const Point2D& a, const Point2D& b) {
    m_landing = mirrorAcross(m_landing, a, b);
    for (auto& leg : m_legs) {
        for (auto& p : leg) p = mirrorAcross(p, a, b);
    }
}

std::vector<Point2D> MLeaderEntity::gripPoints() const {
    std::vector<Point2D> pts{m_landing};
    for (const auto& leg : m_legs) {
        for (const Point2D& p : leg) pts.push_back(p);
    }
    return pts;
}

void MLeaderEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) {
        m_landing = newPos;
        return;
    }
    std::size_t remaining = index - 1;
    for (auto& leg : m_legs) {
        if (remaining < leg.size()) {
            leg[remaining] = newPos;
            return;
        }
        remaining -= leg.size();
    }
}

std::vector<SnapPoint> MLeaderEntity::snapCandidates() const {
    std::vector<SnapPoint> result;
    result.push_back({m_landing, SnapKind::Endpoint});
    for (const auto& leg : m_legs) {
        for (const Point2D& p : leg) result.push_back({p, SnapKind::Endpoint});
    }
    return result;
}

std::unique_ptr<Entity> MLeaderEntity::clone() const { return std::make_unique<MLeaderEntity>(*this); }

} // namespace lcad
