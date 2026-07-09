#include "commands/RectangCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Polyline.h"

#include <cmath>

QString RectangCommand::start() {
    return QStringLiteral("RECTANG  Specify first corner point:");
}

std::optional<QString> RectangCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_hasFirst) {
        m_first = pt;
        m_hasFirst = true;
        return QStringLiteral("Specify other corner point:");
    }

    if (std::abs(pt.x - m_first.x) < 1e-6 || std::abs(pt.y - m_first.y) < 1e-6) {
        return QStringLiteral("Specify other corner point:"); // zero-area rectangle, re-prompt
    }

    std::vector<lcad::Point2D> verts{
        m_first,
        lcad::Point2D(pt.x, m_first.y),
        pt,
        lcad::Point2D(m_first.x, pt.y),
    };
    const auto id = m_document.reserveEntityId();
    auto entity = std::make_unique<lcad::PolylineEntity>(id, m_document.currentLayer(), std::move(verts), true);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
    m_finished = true;
    return std::nullopt;
}

void RectangCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> RectangCommand::previewSegments() const {
    if (!m_hasFirst || !m_hasPreview) return {};
    const lcad::Point2D a = m_first;
    const lcad::Point2D b(m_previewPoint.x, m_first.y);
    const lcad::Point2D c = m_previewPoint;
    const lcad::Point2D d(m_first.x, m_previewPoint.y);
    return {{a, b}, {b, c}, {c, d}, {d, a}};
}
