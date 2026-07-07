#include "commands/MoveCommand.h"

#include "core/document/Commands.h"

QString MoveCommand::start() {
    return QStringLiteral("MOVE  %1 found\nSpecify base point:").arg(static_cast<int>(m_ids.size()));
}

std::optional<QString> MoveCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_hasBase) {
        m_base = pt;
        m_hasBase = true;
        return QStringLiteral("Specify second point:");
    }

    const lcad::Point2D delta = pt - m_base;
    m_document.commandStack().execute(std::make_unique<lcad::TranslateEntitiesCommand>(m_document, m_ids, delta));
    m_finished = true;
    return std::nullopt;
}

void MoveCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> MoveCommand::previewSegments() const {
    if (m_hasBase && m_hasPreview) return {{m_base, m_previewPoint}};
    return {};
}
