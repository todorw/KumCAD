#include "commands/AttEditCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Insert.h"

QString AttEditCommand::listing() const {
    const lcad::Entity* e = m_document.findEntity(m_targetId);
    const auto* insert = dynamic_cast<const lcad::InsertEntity*>(e);
    if (!insert) return QString();
    QString text;
    for (const auto& [tag, value] : insert->attributes()) {
        text += QStringLiteral("  %1 = %2\n").arg(QString::fromStdString(tag), QString::fromStdString(value));
    }
    return text;
}

QString AttEditCommand::start() {
    return QStringLiteral("ATTEDIT\n%1Enter tag to edit, or Enter to finish:").arg(listing());
}

std::optional<QString> AttEditCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();

    if (m_awaitingValue) {
        if (!trimmed.isEmpty()) {
            m_document.commandStack().execute(std::make_unique<lcad::SetInsertAttributeCommand>(
                m_document, m_targetId, m_pendingTag, trimmed.toStdString()));
        }
        m_awaitingValue = false;
        return QStringLiteral("%1Enter tag to edit, or Enter to finish:").arg(listing());
    }

    if (trimmed.isEmpty()) {
        m_finished = true;
        return QStringLiteral("*Done*");
    }

    const lcad::Entity* e = m_document.findEntity(m_targetId);
    const auto* insert = dynamic_cast<const lcad::InsertEntity*>(e);
    if (!insert) {
        m_finished = true;
        return QStringLiteral("*Target is no longer a valid block reference*");
    }

    std::string matchedTag;
    std::string currentValue;
    bool found = false;
    for (const auto& [tag, value] : insert->attributes()) {
        if (QString::fromStdString(tag).compare(trimmed, Qt::CaseInsensitive) == 0) {
            matchedTag = tag;
            currentValue = value;
            found = true;
            break;
        }
    }
    if (!found) return QStringLiteral("*No attribute tagged \"%1\"*\n%2").arg(trimmed, listing());

    m_pendingTag = matchedTag;
    m_awaitingValue = true;
    return QStringLiteral("New value for %1 <%2>:").arg(QString::fromStdString(matchedTag),
                                                         QString::fromStdString(currentValue));
}
