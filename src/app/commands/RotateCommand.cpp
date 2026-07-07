#include "commands/RotateCommand.h"

#include "core/document/Commands.h"

#include <cmath>

QString RotateCommand::start() {
    return QStringLiteral("ROTATE  %1 found\nSpecify base point:").arg(static_cast<int>(m_ids.size()));
}

std::optional<QString> RotateCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_hasBase) {
        m_base = pt;
        m_hasBase = true;
        return QStringLiteral("Specify rotation angle:");
    }

    const double angle = std::atan2(pt.y - m_base.y, pt.x - m_base.x);
    m_document.commandStack().execute(
        std::make_unique<lcad::RotateEntitiesCommand>(m_document, m_ids, m_base, angle));
    m_finished = true;
    return std::nullopt;
}

std::optional<QString> RotateCommand::onScalar(double value) {
    if (!m_hasBase) return std::nullopt; // need a base point first

    const double angle = value * M_PI / 180.0; // typed rotation angles are in degrees
    m_document.commandStack().execute(
        std::make_unique<lcad::RotateEntitiesCommand>(m_document, m_ids, m_base, angle));
    m_finished = true;
    return std::nullopt;
}

void RotateCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> RotateCommand::previewSegments() const {
    if (m_hasBase && m_hasPreview) return {{m_base, m_previewPoint}};
    return {};
}
