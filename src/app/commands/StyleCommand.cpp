#include "commands/StyleCommand.h"

#include <QStringList>

QString StyleCommand::start() {
    QStringList names;
    for (const lcad::TextStyle& s : m_document.textStyles()) names << QString::fromStdString(s.name);
    return QStringLiteral("STYLE  Styles: %1\nStyle name (new name creates) <%2>:")
        .arg(names.join(QStringLiteral(", ")), QString::fromStdString(m_document.currentTextStyleName()));
}

std::optional<QString> StyleCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();

    switch (m_stage) {
    case Stage::Name: {
        const std::string name = trimmed.isEmpty() ? m_document.currentTextStyleName() : trimmed.toStdString();
        if (const lcad::TextStyle* existing = m_document.findTextStyle(name)) {
            m_style = *existing;
        } else {
            m_style = lcad::TextStyle{};
            m_style.name = name;
        }
        m_stage = Stage::Font;
        return QStringLiteral("Font family <%1>:").arg(QString::fromStdString(m_style.font));
    }
    case Stage::Font:
        if (!trimmed.isEmpty()) m_style.font = trimmed.toStdString();
        m_stage = Stage::Height;
        return QStringLiteral("Fixed height, 0 = prompt each TEXT <%1>:").arg(m_style.fixedHeight);
    case Stage::Height: {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const double v = trimmed.toDouble(&ok);
            if (!ok || v < 0.0) return QStringLiteral("*Invalid* Fixed height <%1>:").arg(m_style.fixedHeight);
            m_style.fixedHeight = v;
        }
        m_stage = Stage::WidthFactor;
        return QStringLiteral("Width factor <%1>:").arg(m_style.widthFactor);
    }
    case Stage::WidthFactor: {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const double v = trimmed.toDouble(&ok);
            if (!ok || v < 0.1 || v > 10.0) return QStringLiteral("*Invalid* Width factor <%1>:").arg(m_style.widthFactor);
            m_style.widthFactor = v;
        }
        m_stage = Stage::Oblique;
        return QStringLiteral("Oblique angle (degrees) <%1>:").arg(m_style.obliqueDeg);
    }
    case Stage::Oblique: {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const double v = trimmed.toDouble(&ok);
            if (!ok || v < -85.0 || v > 85.0) return QStringLiteral("*Invalid* Oblique angle <%1>:").arg(m_style.obliqueDeg);
            m_style.obliqueDeg = v;
        }
        m_document.addOrUpdateTextStyle(m_style);
        m_document.setCurrentTextStyle(m_style.name);
        m_finished = true;
        return QStringLiteral("*Style \"%1\" is current: %2, height %3, width %4, oblique %5*")
            .arg(QString::fromStdString(m_style.name), QString::fromStdString(m_style.font))
            .arg(m_style.fixedHeight)
            .arg(m_style.widthFactor)
            .arg(m_style.obliqueDeg);
    }
    }
    return std::nullopt;
}
