#include "commands/PolylineCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Polyline.h"

#include <cmath>

namespace {

double normalizeToPi(double a) {
    a = std::fmod(a, 2 * M_PI);
    if (a > M_PI) a -= 2 * M_PI;
    if (a < -M_PI) a += 2 * M_PI;
    return a;
}

// Chops the segment into short chords for the rubber-band preview.
void appendPreviewChords(std::vector<std::pair<lcad::Point2D, lcad::Point2D>>& segs, const lcad::Point2D& a,
                         const lcad::Point2D& b, double bulge) {
    const auto arc = lcad::bulgeToArc(a, b, bulge);
    if (!arc) {
        segs.emplace_back(a, b);
        return;
    }
    const int steps = std::max(2, static_cast<int>(std::ceil(std::abs(arc->sweep) / (M_PI / 16))));
    lcad::Point2D prev = a;
    for (int i = 1; i <= steps; ++i) {
        const double angle = arc->startAngle + arc->sweep * i / steps;
        const lcad::Point2D next =
            i == steps ? b : arc->center + lcad::Point2D(std::cos(angle), std::sin(angle)) * arc->radius;
        segs.emplace_back(prev, next);
        prev = next;
    }
}

} // namespace

QString PolylineCommand::start() {
    return QStringLiteral("PLINE  Specify first point:");
}

QString PolylineCommand::prompt() const {
    return m_arcMode ? QStringLiteral("Specify endpoint of arc or [Line/Close/Enter to finish]:")
                     : QStringLiteral("Specify next point or [Arc/Close/Enter to finish]:");
}

double PolylineCommand::bulgeForNextSegment(const lcad::Point2D& to) const {
    if (!m_arcMode || m_points.empty()) return 0.0;
    const lcad::Point2D chord = to - m_points.back();
    if (chord.length() < 1e-9) return 0.0;
    const double chordAngle = std::atan2(chord.y, chord.x);
    // AutoCAD starts the arc tangent to the previous segment; with no history
    // yet it uses the current direction, which defaults to 0 (east).
    const double tangent = m_tangent.value_or(0.0);
    const double theta = 2.0 * normalizeToPi(chordAngle - tangent); // included angle
    if (std::abs(theta) < 1e-9) return 0.0;
    return std::tan(theta / 4.0);
}

std::optional<QString> PolylineCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_points.empty()) {
        const lcad::Point2D chord = pt - m_points.back();
        if (chord.length() < 1e-12) return prompt(); // ignore a duplicate pick
        const double bulge = bulgeForNextSegment(pt);
        m_bulges.back() = bulge;
        const double chordAngle = std::atan2(chord.y, chord.x);
        // Direction at the new endpoint: the chord direction, swung by half
        // the included angle for an arc (its end tangent).
        m_tangent = chordAngle + 2.0 * std::atan(bulge);
    }
    m_points.push_back(pt);
    m_bulges.push_back(0.0);
    return prompt();
}

std::optional<QString> PolylineCommand::onOption(const QString& option) {
    const QString opt = option.toUpper();
    if (opt == QLatin1String("A") || opt == QLatin1String("ARC")) {
        m_arcMode = true;
        return prompt();
    }
    if (opt == QLatin1String("L") || opt == QLatin1String("LINE")) {
        m_arcMode = false;
        return prompt();
    }
    if (opt != QLatin1String("C") && opt != QLatin1String("CLOSE")) return std::nullopt;
    if (m_points.size() < 3) return QStringLiteral("*Need at least three points to close*");
    // Close with the current mode: a tangent arc back to the first point, or
    // a straight segment.
    m_bulges.back() = bulgeForNextSegment(m_points.front());
    commit(true);
    m_finished = true;
    return std::nullopt;
}

void PolylineCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> PolylineCommand::previewSegments() const {
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    for (std::size_t i = 0; i + 1 < m_points.size(); ++i) {
        appendPreviewChords(segs, m_points[i], m_points[i + 1], m_bulges[i]);
    }
    if (!m_points.empty() && m_hasPreview) {
        appendPreviewChords(segs, m_points.back(), m_previewPoint, bulgeForNextSegment(m_previewPoint));
    }
    return segs;
}

void PolylineCommand::commit(bool closed) {
    const auto id = m_document.reserveEntityId();
    auto entity = std::make_unique<lcad::PolylineEntity>(id, m_document.currentLayer(), m_points, m_bulges, closed);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
}

bool PolylineCommand::requestFinish() {
    m_finished = true;
    if (m_points.size() < 2) return false;
    commit(false);
    return true;
}
