#include "commands/PageSetupCommand.h"

namespace {

// Portrait-orientation ISO/ANSI sheet sizes in mm.
struct PaperSize {
    const char* name;
    double width;
    double height;
};

constexpr PaperSize kPaperSizes[] = {
    {"A4", 210.0, 297.0}, {"A3", 297.0, 420.0},   {"A2", 420.0, 594.0},
    {"A1", 594.0, 841.0}, {"A0", 841.0, 1189.0},  {"LETTER", 215.9, 279.4},
};

} // namespace

QString PageSetupCommand::start() {
    return QStringLiteral("PAGESETUP  \"%1\" is %2 x %3 mm\nPaper size [A4/A3/A2/A1/A0/Letter/Custom]:")
        .arg(QString::fromStdString(layout().name))
        .arg(layout().paperWidth)
        .arg(layout().paperHeight);
}

std::optional<QString> PageSetupCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    const QString upper = trimmed.toUpper();

    switch (m_stage) {
    case Stage::Size: {
        if (upper.isEmpty()) {
            m_finished = true;
            return QStringLiteral("*Sheet unchanged*");
        }
        if (upper == QLatin1String("C") || upper == QLatin1String("CUSTOM")) {
            m_stage = Stage::CustomWidth;
            return QStringLiteral("Sheet width (mm) <%1>:").arg(layout().paperWidth);
        }
        for (const PaperSize& size : kPaperSizes) {
            if (upper == QLatin1String(size.name)) {
                m_width = size.width;
                m_height = size.height;
                m_stage = Stage::Orientation;
                return QStringLiteral("Orientation [Landscape/Portrait] <Landscape>:");
            }
        }
        return QStringLiteral("*Unknown size* [A4/A3/A2/A1/A0/Letter/Custom]:");
    }
    case Stage::Orientation: {
        const bool portrait = upper == QLatin1String("P") || upper == QLatin1String("PORTRAIT");
        layout().paperWidth = portrait ? m_width : m_height;
        layout().paperHeight = portrait ? m_height : m_width;
        m_finished = true;
        return QStringLiteral("*Sheet set to %1 x %2 mm*").arg(layout().paperWidth).arg(layout().paperHeight);
    }
    case Stage::CustomWidth: {
        bool ok = false;
        const double v = trimmed.toDouble(&ok);
        if (trimmed.isEmpty()) {
            m_stage = Stage::CustomHeight;
            return QStringLiteral("Sheet height (mm) <%1>:").arg(layout().paperHeight);
        }
        if (!ok || v < 10.0 || v > 5000.0) return QStringLiteral("*Invalid* Sheet width (mm) <%1>:").arg(layout().paperWidth);
        layout().paperWidth = v;
        m_stage = Stage::CustomHeight;
        return QStringLiteral("Sheet height (mm) <%1>:").arg(layout().paperHeight);
    }
    case Stage::CustomHeight: {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const double v = trimmed.toDouble(&ok);
            if (!ok || v < 10.0 || v > 5000.0) {
                return QStringLiteral("*Invalid* Sheet height (mm) <%1>:").arg(layout().paperHeight);
            }
            layout().paperHeight = v;
        }
        m_finished = true;
        return QStringLiteral("*Sheet set to %1 x %2 mm*").arg(layout().paperWidth).arg(layout().paperHeight);
    }
    }
    return std::nullopt;
}
