#include "commands/AttDefCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/AttDef.h"

std::optional<QString> AttDefCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();

    switch (m_stage) {
    case Stage::Tag:
        if (trimmed.isEmpty() || trimmed.contains(QLatin1Char(' '))) {
            return QStringLiteral("*Tag must be non-empty, without spaces*\nEnter tag name:");
        }
        m_tag = trimmed.toUpper();
        m_stage = Stage::Prompt;
        return QStringLiteral("Enter prompt <%1>:").arg(m_tag);
    case Stage::Prompt:
        m_prompt = trimmed.isEmpty() ? m_tag : trimmed;
        m_stage = Stage::Default;
        return QStringLiteral("Enter default value <empty>:");
    case Stage::Default:
        m_default = trimmed;
        m_stage = Stage::Height;
        return QStringLiteral("Text height <%1>:").arg(m_height);
    case Stage::Height: {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const double v = trimmed.toDouble(&ok);
            if (!ok || v < 1e-6) return QStringLiteral("*Invalid* Text height <%1>:").arg(m_height);
            m_height = v;
        }
        m_stage = Stage::Position;
        return QStringLiteral("Specify insertion point:");
    }
    case Stage::Position:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<QString> AttDefCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Position) return std::nullopt;
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(
        m_document,
        std::make_unique<lcad::AttDefEntity>(m_document.reserveEntityId(), m_document.currentLayer(), pt,
                                             m_tag.toStdString(), m_prompt.toStdString(), m_default.toStdString(),
                                             m_height)));
    m_finished = true;
    return QStringLiteral("*Attribute definition \"%1\" placed — include it in a BLOCK*").arg(m_tag);
}
