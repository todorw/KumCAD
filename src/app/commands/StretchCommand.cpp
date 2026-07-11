#include "commands/StretchCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/ModifyOps.h"

QString StretchCommand::start() {
    return QStringLiteral("STRETCH  Specify first corner of crossing window:");
}

std::optional<QString> StretchCommand::onPoint(const lcad::Point2D& pt) {
    switch (m_stage) {
    case Stage::WindowFirst:
        m_windowA = pt;
        m_stage = Stage::WindowSecond;
        return QStringLiteral("Specify opposite corner:");
    case Stage::WindowSecond:
        m_windowB = pt;
        m_stage = Stage::BasePoint;
        return QStringLiteral("Specify base point:");
    case Stage::BasePoint:
        m_base = pt;
        m_stage = Stage::SecondPoint;
        return QStringLiteral("Specify second point:");
    case Stage::SecondPoint: {
        const lcad::Point2D delta = pt - m_base;
        lcad::BoundingBox window;
        window.expand(m_windowA);
        window.expand(m_windowB);

        auto batch = std::make_unique<lcad::BatchCommand>("Stretch");
        int touched = 0;
        for (const lcad::Entity* e : m_document.entities()) {
            const lcad::Layer* layer = m_document.findLayer(e->layer());
            if (layer && (!layer->visible || layer->locked)) continue;
            if (auto stretched = lcad::stretchedClone(*e, window, delta)) {
                batch->add(std::make_unique<lcad::ReplaceEntityCommand>(m_document, e->id(), std::move(stretched)));
                ++touched;
            }
        }
        m_finished = true;
        if (batch->empty()) return QStringLiteral("*Nothing to stretch in that window*");
        m_document.commandStack().execute(std::move(batch));
        return QStringLiteral("*%1 stretched*").arg(touched);
    }
    }
    return std::nullopt;
}

void StretchCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> StretchCommand::previewSegments() const {
    if (!m_hasPreview) return {};
    if (m_stage == Stage::WindowSecond) {
        // Rubber-band rectangle for the crossing window.
        const lcad::Point2D a = m_windowA;
        const lcad::Point2D b = m_previewPoint;
        return {{a, {b.x, a.y}}, {{b.x, a.y}, b}, {b, {a.x, b.y}}, {{a.x, b.y}, a}};
    }
    if (m_stage == Stage::SecondPoint) return {{m_base, m_previewPoint}};
    return {};
}
