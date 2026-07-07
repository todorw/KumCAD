#include "commands/ScaleCommand.h"

#include "core/document/Commands.h"

QString ScaleCommand::start() {
    return QStringLiteral("SCALE  %1 found\nSpecify base point:").arg(static_cast<int>(m_ids.size()));
}

std::optional<QString> ScaleCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_hasBase) {
        m_base = pt;
        m_hasBase = true;
        return QStringLiteral("Specify scale factor:");
    }

    const double factor = m_base.distanceTo(pt);
    if (factor < 1e-6) return QStringLiteral("Specify scale factor:"); // degenerate pick, ignore and re-prompt

    m_document.commandStack().execute(
        std::make_unique<lcad::ScaleEntitiesCommand>(m_document, m_ids, m_base, factor));
    m_finished = true;
    return std::nullopt;
}

void ScaleCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> ScaleCommand::previewSegments() const {
    if (m_hasBase && m_hasPreview) return {{m_base, m_previewPoint}};
    return {};
}
