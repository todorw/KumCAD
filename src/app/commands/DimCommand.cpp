#include "commands/DimCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Dimension.h"

QString DimCommand::start() {
    return QStringLiteral("%1  Specify first extension line origin:")
        .arg(m_aligned ? QStringLiteral("DIMALIGNED") : QStringLiteral("DIMLINEAR"));
}

std::optional<QString> DimCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage == Stage::FirstPoint) {
        m_p1 = pt;
        m_stage = Stage::SecondPoint;
        return QStringLiteral("Specify second extension line origin:");
    }
    if (m_stage == Stage::SecondPoint) {
        if (pt.distanceTo(m_p1) < 1e-9) return QStringLiteral("Specify second extension line origin:");
        m_p2 = pt;
        m_stage = Stage::LinePosition;
        return QStringLiteral("Specify dimension line location:");
    }

    const auto id = m_document.reserveEntityId();
    auto entity = std::make_unique<lcad::DimensionEntity>(id, m_document.currentLayer(), m_p1, m_p2, pt, m_aligned);
    const double value = entity->geometry().value;
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
    m_finished = true;
    return QStringLiteral("Dimension = %1").arg(value, 0, 'f', 2);
}

void DimCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> DimCommand::previewSegments() const {
    if (!m_hasPreview) return {};
    if (m_stage == Stage::SecondPoint) return {{m_p1, m_previewPoint}};
    if (m_stage == Stage::LinePosition) {
        // Preview the dimension line placement via a throwaway entity's geometry.
        const lcad::DimensionEntity preview(0, 0, m_p1, m_p2, m_previewPoint, m_aligned);
        const auto geo = preview.geometry();
        return {{geo.ext1A, geo.ext1B}, {geo.ext2A, geo.ext2B}, {geo.dimA, geo.dimB}};
    }
    return {};
}
