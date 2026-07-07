#include "commands/CopyCommand.h"

#include "core/document/Commands.h"

QString CopyCommand::start() {
    return QStringLiteral("COPY  %1 found\nSpecify base point:").arg(static_cast<int>(m_ids.size()));
}

std::optional<QString> CopyCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_hasBase) {
        m_base = pt;
        m_hasBase = true;
        return QStringLiteral("Specify second point or [Enter to finish]:");
    }

    const lcad::Point2D delta = pt - m_base;
    for (lcad::EntityId id : m_ids) {
        const lcad::Entity* source = m_document.findEntity(id);
        if (!source) continue;
        std::unique_ptr<lcad::Entity> copy = source->clone();
        copy->translate(delta);
        copy->setId(m_document.reserveEntityId());
        m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(copy)));
    }
    return QStringLiteral("Specify second point or [Enter to finish]:");
}

void CopyCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> CopyCommand::previewSegments() const {
    if (m_hasBase && m_hasPreview) return {{m_base, m_previewPoint}};
    return {};
}

bool CopyCommand::requestFinish() {
    m_finished = true;
    return m_hasBase;
}
