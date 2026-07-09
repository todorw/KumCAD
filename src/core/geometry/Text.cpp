#include "core/geometry/Text.h"

#include <cmath>

namespace lcad {

namespace {

double clampedAxisDistance(double p, double lo, double hi) {
    if (p < lo) return lo - p;
    if (p > hi) return p - hi;
    return 0.0;
}

} // namespace

double TextEntity::approximateWidth() const {
    return 0.6 * m_height * static_cast<double>(m_text.size());
}

BoundingBox TextEntity::boundingBox() const {
    const double w = approximateWidth();
    BoundingBox box;
    box.expand(rotateAround(m_position, m_position, m_rotation));
    box.expand(rotateAround(Point2D(m_position.x + w, m_position.y), m_position, m_rotation));
    box.expand(rotateAround(Point2D(m_position.x, m_position.y + m_height), m_position, m_rotation));
    box.expand(rotateAround(Point2D(m_position.x + w, m_position.y + m_height), m_position, m_rotation));
    return box;
}

double TextEntity::distanceTo(const Point2D& pt) const {
    const Point2D local = rotateAround(pt, m_position, -m_rotation) - m_position;
    const double dx = clampedAxisDistance(local.x, 0.0, approximateWidth());
    const double dy = clampedAxisDistance(local.y, 0.0, m_height);
    return std::sqrt(dx * dx + dy * dy);
}

void TextEntity::translate(const Point2D& delta) {
    m_position = m_position + delta;
}

void TextEntity::rotate(const Point2D& center, double angleRadians) {
    m_position = rotateAround(m_position, center, angleRadians);
    m_rotation += angleRadians;
}

void TextEntity::scale(const Point2D& center, double factor) {
    m_position = scaleAround(m_position, center, factor);
    m_height *= factor;
}

void TextEntity::mirror(const Point2D& a, const Point2D& b) {
    // Mirror the insertion point but keep the text readable (AutoCAD's
    // MIRRTEXT=0 behavior): reflect the baseline direction, then flip it back
    // 180 degrees if that left the text upside down.
    m_position = mirrorAcross(m_position, a, b);
    const double phi = std::atan2(b.y - a.y, b.x - a.x);
    double reflected = 2.0 * phi - m_rotation;
    reflected = std::fmod(reflected, 2.0 * M_PI);
    if (reflected < 0) reflected += 2.0 * M_PI;
    if (reflected > M_PI / 2 && reflected <= 3 * M_PI / 2) reflected += M_PI;
    m_rotation = reflected;
}

std::vector<Point2D> TextEntity::gripPoints() const {
    return {m_position};
}

void TextEntity::moveGripPoint(std::size_t index, const Point2D& newPos) {
    if (index == 0) m_position = newPos;
}

std::vector<SnapPoint> TextEntity::snapCandidates() const {
    return {{m_position, SnapKind::Endpoint}};
}

std::unique_ptr<Entity> TextEntity::clone() const {
    return std::make_unique<TextEntity>(*this);
}

} // namespace lcad
