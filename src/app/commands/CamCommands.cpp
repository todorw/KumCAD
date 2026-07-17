#include "commands/CamCommands.h"

#include "core/cam/GCodeWriter.h"
#include "core/geometry/Polyline.h"

std::optional<QString> GCodeExportCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Pick) return std::nullopt;

    const lcad::PolylineEntity* best = nullptr;
    double bestDist = m_pickTolerance;
    for (const lcad::Entity* e : m_document.entities()) {
        if (e->type() != lcad::EntityType::Polyline) continue;
        const auto* pl = static_cast<const lcad::PolylineEntity*>(e);
        if (!pl->closed()) continue;
        const double d = pl->distanceTo(pt);
        if (d <= bestDist) {
            bestDist = d;
            best = pl;
        }
    }
    if (!best) return QStringLiteral("*No closed polyline there*\nSelect a closed polyline profile:");

    m_profileId = best->id();
    m_stage = Stage::ToolDiameter;
    return QStringLiteral("Enter tool diameter:");
}

namespace {
// Parses text as a positive double, or -- if text is empty -- keeps
// fallback (the "press Enter to keep the default" convention already used
// by CutSide's own empty-input-means-OnLine handling).
bool parsePositiveOrDefault(const QString& text, double fallback, double& out) {
    if (text.trimmed().isEmpty()) {
        out = fallback;
        return true;
    }
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || value <= 0.0) return false;
    out = value;
    return true;
}
} // namespace

std::optional<QString> GCodeExportCommand::onText(const QString& text) {
    switch (m_stage) {
    case Stage::ToolDiameter: {
        bool ok = false;
        const double d = text.trimmed().toDouble(&ok);
        if (!ok || d < 0.0) return QStringLiteral("*Invalid tool diameter*");
        m_params.toolDiameter = d;
        m_stage = Stage::CutSide;
        return QStringLiteral("Cut side [Outside/Inside/OnLine] <OnLine>:");
    }
    case Stage::CutSide: {
        const QString side = text.trimmed().toUpper();
        if (side.isEmpty() || side == QLatin1String("ONLINE")) m_params.side = lcad::CutSide::OnLine;
        else if (side == QLatin1String("OUTSIDE")) m_params.side = lcad::CutSide::Outside;
        else if (side == QLatin1String("INSIDE")) m_params.side = lcad::CutSide::Inside;
        else return QStringLiteral("*Unrecognized cut side*\nCut side <OnLine>:");
        m_stage = Stage::FeedRate;
        return QStringLiteral("Feed rate <%1>:").arg(m_params.feedRate);
    }
    case Stage::FeedRate: {
        double value = 0.0;
        if (!parsePositiveOrDefault(text, m_params.feedRate, value)) return QStringLiteral("*Invalid feed rate*");
        m_params.feedRate = value;
        m_stage = Stage::PlungeRate;
        return QStringLiteral("Plunge rate <%1>:").arg(m_params.plungeRate);
    }
    case Stage::PlungeRate: {
        double value = 0.0;
        if (!parsePositiveOrDefault(text, m_params.plungeRate, value)) return QStringLiteral("*Invalid plunge rate*");
        m_params.plungeRate = value;
        m_stage = Stage::CutDepth;
        return QStringLiteral("Cut depth <%1>:").arg(m_params.cutDepth);
    }
    case Stage::CutDepth: {
        double value = 0.0;
        if (!parsePositiveOrDefault(text, m_params.cutDepth, value)) return QStringLiteral("*Invalid cut depth*");
        m_params.cutDepth = value;
        m_stage = Stage::SafeHeight;
        return QStringLiteral("Safe height <%1>:").arg(m_params.safeHeight);
    }
    case Stage::SafeHeight: {
        double value = 0.0;
        if (!parsePositiveOrDefault(text, m_params.safeHeight, value)) return QStringLiteral("*Invalid safe height*");
        m_params.safeHeight = value;
        m_stage = Stage::Path;
        return QStringLiteral("Enter output G-code file path:");
    }
    case Stage::Path: {
        m_finished = true;
        const auto* profile = static_cast<const lcad::PolylineEntity*>(m_document.findEntity(m_profileId));
        if (!profile) return QStringLiteral("*Profile no longer exists*");

        const std::vector<lcad::Point2D> path = lcad::computeToolpath(*profile, m_params);
        if (path.size() < 2) return QStringLiteral("*Toolpath computation failed (tool too large for a tight corner?)*");

        std::string error;
        if (!lcad::writeGCode(path, m_params, text.trimmed().toStdString(), &error)) {
            return QStringLiteral("*%1*").arg(QString::fromStdString(error));
        }
        return QStringLiteral("*G-code written to %1 (%2 point(s))*").arg(text.trimmed()).arg(path.size());
    }
    default:
        return std::nullopt;
    }
}
