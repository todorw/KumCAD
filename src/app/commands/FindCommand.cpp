#include "commands/FindCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/MText.h"
#include "core/geometry/Text.h"

std::optional<QString> FindCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();

    if (m_stage == Stage::Find) {
        if (trimmed.isEmpty()) {
            m_finished = true;
            return QStringLiteral("*Nothing to find*");
        }
        m_findText = trimmed;
        m_stage = Stage::Replace;
        return QStringLiteral("Replace with (Enter to find only, no edits):");
    }

    // Stage::Replace
    m_doReplace = !trimmed.isEmpty();
    m_replaceText = trimmed;
    runSearch();
    m_finished = true;

    if (!m_doReplace) return QStringLiteral("*%1 match(es) found and selected*").arg(m_matches.size());
    return QStringLiteral("*%1 match(es), replaced*").arg(m_matches.size());
}

void FindCommand::runSearch() {
    m_matches.clear();
    auto batch = std::make_unique<lcad::BatchCommand>("Find/Replace");

    for (lcad::Entity* e : m_document.entities()) {
        QString content;
        if (e->type() == lcad::EntityType::Text) {
            content = QString::fromStdString(static_cast<lcad::TextEntity*>(e)->text());
        } else if (e->type() == lcad::EntityType::MText) {
            content = QString::fromStdString(static_cast<lcad::MTextEntity*>(e)->text());
        } else {
            continue;
        }
        if (!content.contains(m_findText, Qt::CaseInsensitive)) continue;
        m_matches.push_back(e->id());

        if (m_doReplace) {
            QString updated = content;
            updated.replace(m_findText, m_replaceText, Qt::CaseInsensitive);
            std::unique_ptr<lcad::Entity> clone = e->clone();
            if (e->type() == lcad::EntityType::Text) {
                static_cast<lcad::TextEntity&>(*clone).setText(updated.toStdString());
            } else {
                static_cast<lcad::MTextEntity&>(*clone).setText(updated.toStdString());
            }
            batch->add(std::make_unique<lcad::ReplaceEntityCommand>(m_document, e->id(), std::move(clone)));
        }
    }
    if (!batch->empty()) m_document.commandStack().execute(std::move(batch));
}
