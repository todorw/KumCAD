#include "commands/MLineCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/MLine.h"

namespace {

// Real AutoCAD's own default "STANDARD" MLSTYLE: 2 parallel lines at
// +0.5/-0.5 from the centerline. Justification shifts every element by a
// constant so the TOP element (the most positive raw offset) sits exactly
// on the path for Top, the BOTTOM element (most negative) for Bottom, or
// neither for Zero (centered, the raw offsets as defined) -- the real
// AutoCAD semantic: the picked path always traces the justified edge/
// centerline, the rest of the style hangs to one side of it.
std::vector<lcad::MLineElement> standardStyleElements(int justification) {
    std::vector<lcad::MLineElement> elements = {{0.5, lcad::Color{255, 255, 255}}, {-0.5, lcad::Color{255, 255, 255}}};
    double shift = 0.0;
    if (justification == 0) shift = -0.5;      // Top: subtract the max offset (0.5)
    else if (justification == 2) shift = 0.5; // Bottom: subtract the min offset (-0.5)
    for (lcad::MLineElement& e : elements) e.offset += shift;
    return elements;
}

} // namespace

QString MLineCommand::start() { return QStringLiteral("MLINE  Specify start point or [Justification/Scale]:"); }

QString MLineCommand::prompt() const {
    return m_points.size() < 2 ? QStringLiteral("Specify next point:")
                               : QStringLiteral("Specify next point or [Close/Undo]:");
}

std::optional<QString> MLineCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Points) return std::nullopt; // ignore a click while awaiting Scale/Justification input
    if (!m_points.empty() && (pt - m_points.back()).length() < 1e-12) return prompt(); // ignore a duplicate pick
    m_points.push_back(pt);
    return prompt();
}

std::optional<QString> MLineCommand::onOption(const QString& option) {
    const QString opt = option.trimmed().toUpper();

    if (m_stage == Stage::AwaitingJustification) {
        if (opt == QLatin1String("T") || opt == QLatin1String("TOP")) m_justification = Justification::Top;
        else if (opt == QLatin1String("Z") || opt == QLatin1String("ZERO")) m_justification = Justification::Zero;
        else if (opt == QLatin1String("B") || opt == QLatin1String("BOTTOM")) m_justification = Justification::Bottom;
        else return std::nullopt;
        m_stage = Stage::Points;
        return m_points.empty() ? start() : prompt();
    }

    if (opt == QLatin1String("J") || opt == QLatin1String("JUSTIFICATION")) {
        m_stage = Stage::AwaitingJustification;
        return QStringLiteral("Enter justification type [Top/Zero/Bottom]:");
    }
    if (opt == QLatin1String("S") || opt == QLatin1String("SCALE")) {
        m_stage = Stage::AwaitingScale;
        return QStringLiteral("Enter mline scale <%1>:").arg(m_scale);
    }
    if (opt == QLatin1String("U") || opt == QLatin1String("UNDO")) {
        if (m_points.empty()) return QStringLiteral("*Nothing to undo*\n%1").arg(start());
        m_points.pop_back();
        return m_points.empty() ? start() : prompt();
    }
    if (opt == QLatin1String("C") || opt == QLatin1String("CLOSE")) {
        if (m_points.size() < 3) return QStringLiteral("*Need at least three points to close*");
        commit(true);
        m_finished = true;
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<QString> MLineCommand::onScalar(double value) {
    if (m_stage != Stage::AwaitingScale) return std::nullopt;
    m_scale = value;
    m_stage = Stage::Points;
    return m_points.empty() ? start() : prompt();
}

void MLineCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> MLineCommand::previewSegments() const {
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    for (std::size_t i = 0; i + 1 < m_points.size(); ++i) segs.emplace_back(m_points[i], m_points[i + 1]);
    if (!m_points.empty() && m_hasPreview) segs.emplace_back(m_points.back(), m_previewPoint);
    return segs;
}

void MLineCommand::commit(bool closed) {
    const auto id = m_document.reserveEntityId();
    auto entity = std::make_unique<lcad::MLineEntity>(id, m_document.currentLayer(), m_points,
                                                       standardStyleElements(static_cast<int>(m_justification)),
                                                       m_scale, closed);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
}

bool MLineCommand::requestFinish() {
    if (m_stage != Stage::Points) { // Enter while a Scale/Justification sub-prompt is pending: cancel it, not the whole command
        m_stage = Stage::Points;
        return true;
    }
    m_finished = true;
    if (m_points.size() < 2) return false;
    commit(false);
    return true;
}
