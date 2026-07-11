#include "commands/MviewCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/BoundingBox.h"

#include <algorithm>
#include <cmath>

std::optional<QString> MviewCommand::onPoint(const lcad::Point2D& pt) {
    if (!m_haveCorner1) {
        m_corner1 = pt;
        m_haveCorner1 = true;
        return QStringLiteral("Specify opposite corner:");
    }

    const double w = std::abs(pt.x - m_corner1.x);
    const double h = std::abs(pt.y - m_corner1.y);
    if (w < 1e-6 || h < 1e-6) return QStringLiteral("Specify opposite corner:");

    lcad::Viewport vp;
    vp.paperCenter = (m_corner1 + pt) * 0.5;
    vp.paperWidth = w;
    vp.paperHeight = h;

    // Fit the whole model into the new viewport, like a fresh zoom-extents.
    lcad::BoundingBox box;
    for (const lcad::Entity* e : m_document.entities()) box.expand(e->boundingBox());
    if (box.isValid()) {
        vp.modelCenter = lcad::Point2D((box.min.x + box.max.x) / 2.0, (box.min.y + box.max.y) / 2.0);
        const double mw = std::max(box.max.x - box.min.x, 1e-6);
        const double mh = std::max(box.max.y - box.min.y, 1e-6);
        vp.viewScale = std::min(w / (mw * 1.05), h / (mh * 1.05));
    }

    if (m_layoutIndex >= 0 && m_layoutIndex < static_cast<int>(m_document.layouts().size())) {
        std::vector<lcad::Layout> layouts = m_document.layouts();
        layouts[static_cast<std::size_t>(m_layoutIndex)].viewports.push_back(vp);
        m_document.commandStack().execute(std::make_unique<lcad::SetLayoutsCommand>(m_document, std::move(layouts)));
    }
    m_finished = true;
    return QStringLiteral("*Viewport created (scale %1) -- use VPSCALE to change it*").arg(vp.viewScale);
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> MviewCommand::previewSegments() const {
    if (!m_haveCorner1 || !m_hasPreview) return {};
    const lcad::Point2D a = m_corner1;
    const lcad::Point2D b = m_previewPoint;
    return {{a, {b.x, a.y}}, {{b.x, a.y}, b}, {b, {a.x, b.y}}, {{a.x, b.y}, a}};
}
