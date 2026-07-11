#include "commands/OsnapCommand.h"

#include "DrawingView.h"

#include <QStringList>

namespace {

struct ModeEntry {
    lcad::SnapKind kind;
    const char* keyword;
    const char* label;
};

constexpr ModeEntry kModes[] = {
    {lcad::SnapKind::Endpoint, "END", "ENDpoint"},
    {lcad::SnapKind::Midpoint, "MID", "MIDpoint"},
    {lcad::SnapKind::Center, "CEN", "CENter"},
    {lcad::SnapKind::Quadrant, "QUA", "QUAdrant"},
    {lcad::SnapKind::Node, "NOD", "NODe"},
    {lcad::SnapKind::Intersection, "INT", "INTersection"},
    {lcad::SnapKind::Perpendicular, "PER", "PERpendicular"},
    {lcad::SnapKind::Tangent, "TAN", "TANgent"},
    {lcad::SnapKind::Nearest, "NEA", "NEArest"},
};

} // namespace

QString OsnapCommand::statusLine() const {
    QStringList parts;
    for (const ModeEntry& mode : kModes) {
        parts << QStringLiteral("%1=%2").arg(QLatin1String(mode.keyword),
                                             m_view.snapModeEnabled(mode.kind) ? QStringLiteral("on")
                                                                               : QStringLiteral("off"));
    }
    return parts.join(QStringLiteral("  "));
}

QString OsnapCommand::start() {
    return QStringLiteral("OSNAP  %1\nToggle a mode (END/MID/CEN/QUA/NOD/INT/PER/TAN/NEA), ALL, NONE, or Enter:")
        .arg(statusLine());
}

std::optional<QString> OsnapCommand::onOption(const QString& option) {
    const QString opt = option.toUpper();
    if (opt == QLatin1String("ALL") || opt == QLatin1String("NONE")) {
        const bool on = opt == QLatin1String("ALL");
        for (const ModeEntry& mode : kModes) m_view.setSnapModeEnabled(mode.kind, on);
        return statusLine() + QStringLiteral("\nToggle a mode, ALL, NONE, or Enter:");
    }
    for (const ModeEntry& mode : kModes) {
        if (opt == QLatin1String(mode.keyword) || opt == QString::fromLatin1(mode.label).toUpper()) {
            m_view.setSnapModeEnabled(mode.kind, !m_view.snapModeEnabled(mode.kind));
            return statusLine() + QStringLiteral("\nToggle a mode, ALL, NONE, or Enter:");
        }
    }
    return std::nullopt;
}
