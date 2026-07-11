#include "commands/DimStyleCommand.h"

#include <QStringList>

QString DimStyleCommand::start() {
    QStringList names;
    for (const lcad::NamedDimStyle& s : m_document.dimStyles()) names << QString::fromStdString(s.name);
    return QStringLiteral("DIMSTYLE  Styles: %1\nStyle name (new name creates) <%2>:")
        .arg(names.join(QStringLiteral(", ")), QString::fromStdString(m_document.currentDimStyleName()));
}

std::optional<QString> DimStyleCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();

    switch (m_stage) {
    case Stage::Name: {
        m_styleName = trimmed.isEmpty() ? m_document.currentDimStyleName() : trimmed.toStdString();
        if (const lcad::NamedDimStyle* existing = m_document.findDimStyle(m_styleName)) {
            m_style = existing->style;
        } else {
            m_style = m_document.dimStyle(); // seed a new style from the current one
        }
        m_stage = Stage::TextHeight;
        return QStringLiteral("Text height <%1>:").arg(m_style.textHeight);
    }
    case Stage::TextHeight: {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const double v = trimmed.toDouble(&ok);
            if (!ok || v < 1e-6) return QStringLiteral("*Invalid* Text height <%1>:").arg(m_style.textHeight);
            m_style.textHeight = v;
        }
        m_stage = Stage::ArrowSize;
        return QStringLiteral("Arrow size <%1>:").arg(m_style.arrowSize);
    }
    case Stage::ArrowSize: {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const double v = trimmed.toDouble(&ok);
            if (!ok || v < 1e-6) return QStringLiteral("*Invalid* Arrow size <%1>:").arg(m_style.arrowSize);
            m_style.arrowSize = v;
        }
        m_stage = Stage::Decimals;
        return QStringLiteral("Decimal places <%1>:").arg(m_style.decimals);
    }
    case Stage::Decimals: {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const int v = trimmed.toInt(&ok);
            if (!ok || v < 0 || v > 8) return QStringLiteral("*Invalid* Decimal places <%1>:").arg(m_style.decimals);
            m_style.decimals = v;
        }
        m_document.addOrUpdateDimStyle(m_styleName, m_style);
        m_document.setCurrentDimStyle(m_styleName);
        m_finished = true;
        return QStringLiteral("*DIMSTYLE \"%1\" is current: text %2, arrows %3, %4 decimals*")
            .arg(QString::fromStdString(m_styleName))
            .arg(m_style.textHeight)
            .arg(m_style.arrowSize)
            .arg(m_style.decimals);
    }
    }
    return std::nullopt;
}
