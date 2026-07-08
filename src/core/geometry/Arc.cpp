#include "core/geometry/Arc.h"

#include <algorithm>
#include <cmath>

namespace lcad {

namespace {

constexpr double kTwoPi = 2.0 * M_PI;

double normalizeAngle(double angle) {
    angle = std::fmod(angle, kTwoPi);
    if (angle < 0) angle += kTwoPi;
    return angle;
}

} // namespace

Point2D ArcEntity::startPoint() const {
    return {m_center.x + m_radius * std::cos(m_startAngle), m_center.y + m_radius * std::sin(m_startAngle)};
}

Point2D ArcEntity::endPoint() const {
    return {m_center.x + m_radius * std::cos(m_endAngle), m_center.y + m_radius * std::sin(m_endAngle)};
}

bool ArcEntity::angleInSweep(double angle) const {
    const double a = normalizeAngle(angle);
    const double start = normalizeAngle(m_startAngle);
    const double end = normalizeAngle(m_endAngle);
    if (start <= end) return a >= start && a <= end;
    return a >= start || a <= end; // sweep wraps past 0
}

BoundingBox ArcEntity::boundingBox() const {
    BoundingBox box;
    box.expand(startPoint());
    box.expand(endPoint());
    box.expand(m_center); // conservative: keeps the box sane for degenerate/zero-length arcs
    // Cardinal points are where the arc extends furthest from the chord.
    for (double angle : {0.0, M_PI / 2, M_PI, 3 * M_PI / 2}) {
        if (angleInSweep(angle)) {
            box.expand(Point2D(m_center.x + m_radius * std::cos(angle), m_center.y + m_radius * std::sin(angle)));
        }
    }
    return box;
}

double ArcEntity::distanceTo(const Point2D& pt) const {
    const double angle = std::atan2(pt.y - m_center.y, pt.x - m_center.x);
    if (angleInSweep(angle)) return std::abs(pt.distanceTo(m_center) - m_radius);
    return std::min(pt.distanceTo(startPoint()), pt.distanceTo(endPoint()));
}

void ArcEntity::translate(const Point2D& delta) {
    m_center = m_center + delta;
}

void ArcEntity::rotate(const Point2D& center, double angleRadians) {
    m_center = rotateAround(m_center, center, angleRadians);
    m_startAngle += angleRadians;
    m_endAngle += angleRadians;
}

void ArcEntity::scale(const Point2D& center, double factor) {
    m_center = scaleAround(m_center, center, factor);
    m_radius *= factor;
}

std::vector<Point2D> ArcEntity::gripPoints() const {
    return {startPoint(), endPoint(), m_center};
}

void ArcEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) {
        m_startAngle = std::atan2(newPos.y - m_center.y, newPos.x - m_center.x);
    } else if (index == 1) {
        m_endAngle = std::atan2(newPos.y - m_center.y, newPos.x - m_center.x);
    } else if (index == 2) {
        translate(newPos - m_center);
    }
}

std::vector<SnapPoint> ArcEntity::snapCandidates() const {
    const double ns = normalizeAngle(m_startAngle);
    const double ne = normalizeAngle(m_endAngle);
    double sweep = ne - ns;
    if (sweep <= 0) sweep += kTwoPi;
    const double midAngle = m_startAngle + sweep / 2.0;
    const Point2D midPoint(m_center.x + m_radius * std::cos(midAngle), m_center.y + m_radius * std::sin(midAngle));

    std::vector<SnapPoint> result{
        {startPoint(), SnapKind::Endpoint},
        {endPoint(), SnapKind::Endpoint},
        {midPoint, SnapKind::Midpoint},
        {m_center, SnapKind::Center},
    };
    for (double angle : {0.0, M_PI / 2, M_PI, 3 * M_PI / 2}) {
        if (angleInSweep(angle)) {
            result.push_back(
                {Point2D(m_center.x + m_radius * std::cos(angle), m_center.y + m_radius * std::sin(angle)), SnapKind::Quadrant});
        }
    }
    return result;
}

std::unique_ptr<Entity> ArcEntity::clone() const {
    return std::make_unique<ArcEntity>(*this);
}

} // namespace lcad
