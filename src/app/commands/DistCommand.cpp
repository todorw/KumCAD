#include "commands/DistCommand.h"

#include <cmath>

QString DistCommand::start() {
    return QStringLiteral("DIST  Specify first point:");
}

std::optional<QString> DistCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_hasFirst) {
        m_first = pt;
        m_hasFirst = true;
        return QStringLiteral("Specify second point:");
    }

    const double dx = pt.x - m_first.x;
    const double dy = pt.y - m_first.y;
    const double dist = std::sqrt(dx * dx + dy * dy);
    double angleDeg = std::atan2(dy, dx) * 180.0 / M_PI;
    if (angleDeg < 0) angleDeg += 360.0;

    m_finished = true;
    return QStringLiteral("Distance = %1,  Delta X = %2,  Delta Y = %3,  Angle = %4\xC2\xB0")
        .arg(dist, 0, 'f', 4)
        .arg(dx, 0, 'f', 4)
        .arg(dy, 0, 'f', 4)
        .arg(angleDeg, 0, 'f', 2);
}

void DistCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> DistCommand::previewSegments() const {
    if (m_hasFirst && m_hasPreview) return {{m_first, m_previewPoint}};
    return {};
}
