#include "commands/LayerStateCommand.h"

#include "core/document/Commands.h"

std::optional<QString> LayerStateCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (m_stage == Stage::Action) {
        if (trimmed.isEmpty()) {
            m_finished = true;
            return QStringLiteral("*Cancelled*");
        }
        const QString lower = trimmed.toLower();
        if (lower == QLatin1String("?")) {
            QString list = QStringLiteral("*Layer states:*");
            for (const lcad::LayerState& state : m_document.layerStates()) {
                list += QStringLiteral(" %1").arg(QString::fromStdString(state.name));
            }
            if (m_document.layerStates().empty()) list += QStringLiteral(" (none)");
            return list + QStringLiteral("\nEnter option [Save/Restore/Delete/?]:");
        }
        if (lower == QLatin1String("s") || lower == QLatin1String("save")) {
            m_action = Action::Save;
        } else if (lower == QLatin1String("r") || lower == QLatin1String("restore")) {
            m_action = Action::Restore;
        } else if (lower == QLatin1String("d") || lower == QLatin1String("delete")) {
            m_action = Action::Delete;
        } else {
            return std::nullopt;
        }
        m_stage = Stage::Name;
        return QStringLiteral("Enter layer state name:");
    }

    if (trimmed.isEmpty()) {
        m_finished = true;
        return QStringLiteral("*Cancelled*");
    }
    const std::string name = trimmed.toStdString();
    m_finished = true;
    switch (m_action) {
    case Action::Save:
        m_document.saveLayerState(name);
        return QStringLiteral("*Layer state \"%1\" saved*").arg(trimmed);
    case Action::Restore:
        for (const lcad::LayerState& state : m_document.layerStates()) {
            if (state.name == name) {
                m_document.commandStack().execute(std::make_unique<lcad::RestoreLayerStateCommand>(m_document, state));
                return QStringLiteral("*Layer state \"%1\" restored*").arg(trimmed);
            }
        }
        return QStringLiteral("*No layer state named \"%1\"*").arg(trimmed);
    case Action::Delete:
        if (m_document.deleteLayerState(name)) {
            return QStringLiteral("*Layer state \"%1\" deleted*").arg(trimmed);
        }
        return QStringLiteral("*No layer state named \"%1\"*").arg(trimmed);
    }
    return std::nullopt;
}
