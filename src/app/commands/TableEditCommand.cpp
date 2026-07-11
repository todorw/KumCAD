#include "commands/TableEditCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Table.h"

std::optional<QString> TableEditCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Select) return std::nullopt;

    const std::vector<lcad::Entity*> entities = m_document.entities();
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        lcad::Entity* e = *it;
        if (e->type() != lcad::EntityType::Table) continue;
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue;
        auto* table = static_cast<lcad::TableEntity*>(e);
        if (const auto cell = table->cellAt(pt)) {
            m_tableId = e->id();
            m_row = cell->first;
            m_col = cell->second;
            m_stage = Stage::Text;
            const QString current = QString::fromStdString(table->cellText(m_row, m_col));
            return QStringLiteral("Cell [%1,%2] <%3>. Enter new text:").arg(m_row + 1).arg(m_col + 1).arg(current);
        }
    }
    return QStringLiteral("*No table cell there*\nSelect table cell:");
}

std::optional<QString> TableEditCommand::onText(const QString& text) {
    if (m_stage != Stage::Text) return std::nullopt;

    if (!text.isEmpty()) {
        lcad::Entity* e = m_document.findEntity(m_tableId);
        if (e && e->type() == lcad::EntityType::Table) {
            auto clone = e->clone();
            static_cast<lcad::TableEntity&>(*clone).setCellText(m_row, m_col, text.toStdString());
            m_document.commandStack().execute(
                std::make_unique<lcad::ReplaceEntityCommand>(m_document, m_tableId, std::move(clone)));
        }
    }
    m_stage = Stage::Select;
    return QStringLiteral("Select table cell (Enter to finish):");
}

bool TableEditCommand::requestFinish() {
    m_finished = true;
    return true;
}
