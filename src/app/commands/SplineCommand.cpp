#include "commands/SplineCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Spline.h"

QString SplineCommand::start() {
    return QStringLiteral("SPLINE  Specify first point:");
}

std::optional<QString> SplineCommand::onPoint(const lcad::Point2D& pt) {
    m_points.push_back(pt);
    return QStringLiteral("Specify next point or [Enter to finish]:");
}

void SplineCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> SplineCommand::previewSegments() const {
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    std::vector<lcad::Point2D> pts = m_points;
    if (m_hasPreview && !pts.empty()) pts.push_back(m_previewPoint);
    if (pts.size() < 2) return segs;

    // Preview the actual interpolated curve, not just the fit polygon.
    if (auto spline = lcad::SplineEntity::fromFitPoints(0, 0, pts)) {
        const auto sampled = spline->sample(64);
        for (std::size_t i = 0; i + 1 < sampled.size(); ++i) segs.emplace_back(sampled[i], sampled[i + 1]);
    } else {
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) segs.emplace_back(pts[i], pts[i + 1]);
    }
    return segs;
}

bool SplineCommand::requestFinish() {
    m_finished = true;
    if (m_points.size() < 2) return false;

    auto entity = lcad::SplineEntity::fromFitPoints(m_document.reserveEntityId(), m_document.currentLayer(), m_points);
    if (!entity) return false;
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
    return true;
}
