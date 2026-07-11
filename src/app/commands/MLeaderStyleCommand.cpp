#include "commands/MLeaderStyleCommand.h"

#include <QStringList>

QString MLeaderStyleCommand::start() {
    QStringList names;
    for (const lcad::NamedMLeaderStyle& s : m_document.mleaderStyles()) names << QString::fromStdString(s.name);
    return QStringLiteral("MLEADERSTYLE  Styles: %1\nStyle name (new name creates) <%2>:")
        .arg(names.join(QStringLiteral(", ")), QString::fromStdString(m_document.currentMLeaderStyleName()));
}

std::optional<QString> MLeaderStyleCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();

    switch (m_stage) {
    case Stage::Name: {
        m_styleName = trimmed.isEmpty() ? m_document.currentMLeaderStyleName() : trimmed.toStdString();
        if (const lcad::NamedMLeaderStyle* existing = m_document.findMLeaderStyle(m_styleName)) {
            m_style = existing->style;
        } else {
            m_style = m_document.mleaderStyle(); // seed a new style from the current one
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
        m_stage = Stage::LandingGap;
        return QStringLiteral("Landing gap (x text height) <%1>:").arg(m_style.landingGap);
    }
    case Stage::LandingGap: {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const double v = trimmed.toDouble(&ok);
            if (!ok || v < 0.0) return QStringLiteral("*Invalid* Landing gap <%1>:").arg(m_style.landingGap);
            m_style.landingGap = v;
        }
        m_document.addOrUpdateMLeaderStyle(m_styleName, m_style);
        m_document.setCurrentMLeaderStyle(m_styleName);
        m_finished = true;
        return QStringLiteral("*MLEADERSTYLE \"%1\" is current: text %2, arrows %3, gap %4*")
            .arg(QString::fromStdString(m_styleName))
            .arg(m_style.textHeight)
            .arg(m_style.arrowSize)
            .arg(m_style.landingGap);
    }
    }
    return std::nullopt;
}
