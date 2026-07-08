#include "commands/EllipseCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Ellipse.h"

#include <cmath>

QString EllipseCommand::start() {
    return QStringLiteral("ELLIPSE  Specify center point:");
}

std::optional<QString> EllipseCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_hasCenter) {
        m_center = pt;
        m_hasCenter = true;
        return QStringLiteral("Specify corner point (sets X/Y radii):");
    }

    const double rx = std::abs(pt.x - m_center.x);
    const double ry = std::abs(pt.y - m_center.y);
    if (rx < 1e-6 || ry < 1e-6) return QStringLiteral("Specify corner point (sets X/Y radii):"); // degenerate, re-prompt

    const auto id = m_document.reserveEntityId();
    auto entity = std::make_unique<lcad::EllipseEntity>(id, m_document.currentLayer(), m_center, rx, ry);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
    m_finished = true;
    return std::nullopt;
}

void EllipseCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> EllipseCommand::previewSegments() const {
    if (m_hasCenter && m_hasPreview) return {{m_center, m_previewPoint}};
    return {};
}
