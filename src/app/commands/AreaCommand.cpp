#include "commands/AreaCommand.h"

#include <cmath>

QString AreaCommand::start() {
    return QStringLiteral("AREA  Specify first corner point:");
}

std::optional<QString> AreaCommand::onPoint(const lcad::Point2D& pt) {
    m_points.push_back(pt);
    return QStringLiteral("Specify next corner point or [Enter to compute]:");
}

bool AreaCommand::requestFinish() {
    m_finished = true;
    if (m_points.size() < 3) {
        m_result = QStringLiteral("*Need at least three points to measure an area*");
        return false;
    }

    double twiceArea = 0.0;
    double perimeter = 0.0;
    for (std::size_t i = 0; i < m_points.size(); ++i) {
        const lcad::Point2D& a = m_points[i];
        const lcad::Point2D& b = m_points[(i + 1) % m_points.size()];
        twiceArea += a.x * b.y - b.x * a.y;
        perimeter += a.distanceTo(b);
    }
    m_result = QStringLiteral("Area = %1,  Perimeter = %2")
                   .arg(std::abs(twiceArea) / 2.0, 0, 'f', 4)
                   .arg(perimeter, 0, 'f', 4);
    return true;
}

void AreaCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> AreaCommand::previewSegments() const {
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    for (std::size_t i = 0; i + 1 < m_points.size(); ++i) segs.emplace_back(m_points[i], m_points[i + 1]);
    if (!m_points.empty() && m_hasPreview) {
        segs.emplace_back(m_points.back(), m_previewPoint);
        if (m_points.size() > 1) segs.emplace_back(m_previewPoint, m_points.front()); // closing hint
    }
    return segs;
}
