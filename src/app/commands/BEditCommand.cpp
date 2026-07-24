#include "commands/BEditCommand.h"

#include "BlockEditorWindow.h"

std::optional<QString> BEditCommand::onText(const QString& text) {
    m_finished = true;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return std::nullopt; // Enter with nothing: cancel

    const std::string name = trimmed.toStdString();
    if (!m_document.findBlock(name)) return QStringLiteral("*No block named \"%1\"*").arg(trimmed);

    auto* window = new BlockEditorWindow(m_document, name, nullptr);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    return QStringLiteral("*Block editor opened for \"%1\"*").arg(trimmed);
}
