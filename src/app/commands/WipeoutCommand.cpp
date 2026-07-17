#include "commands/WipeoutCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Wipeout.h"

std::optional<QString> WipeoutCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_points.empty() && (pt - m_points.back()).length() < 1e-12) {
        return QStringLiteral("Specify next point or [Enter to finish, need at least 3]:");
    }
    m_points.push_back(pt);
    return QStringLiteral("Specify next point or [Enter to finish, need at least 3]:");
}

void WipeoutCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> WipeoutCommand::previewSegments() const {
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    for (std::size_t i = 0; i + 1 < m_points.size(); ++i) segs.emplace_back(m_points[i], m_points[i + 1]);
    if (!m_points.empty() && m_hasPreview) segs.emplace_back(m_points.back(), m_previewPoint);
    return segs;
}

bool WipeoutCommand::requestFinish() {
    m_finished = true;
    if (m_points.size() < 3) {
        m_result = QStringLiteral("*Need at least 3 points*");
        return false;
    }
    const auto id = m_document.reserveEntityId();
    auto entity = std::make_unique<lcad::WipeoutEntity>(id, m_document.currentLayer(), m_points);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
    m_result = QStringLiteral("*Wipeout created*");
    return true;
}
