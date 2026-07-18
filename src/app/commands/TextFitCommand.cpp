#include "commands/TextFitCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Text.h"

#include <cmath>

std::optional<QString> TextFitCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage == 0) {
        m_start = pt;
        m_stage = 1;
        return QStringLiteral("End point:");
    }

    m_finished = true;
    const lcad::Entity* e = m_document.findEntity(m_targetId);
    if (!e || e->type() != lcad::EntityType::Text) {
        m_result = QStringLiteral("*Selected text no longer exists*");
        return m_result;
    }
    const auto& original = static_cast<const lcad::TextEntity&>(*e);

    const double distance = m_start.distanceTo(pt);
    if (distance < 1e-9) {
        m_result = QStringLiteral("*Start and end points coincide*");
        return m_result;
    }
    const double baseWidth = original.approximateWidth() / std::max(original.widthFactor(), 1e-9);
    if (baseWidth < 1e-9) {
        m_result = QStringLiteral("*Text has no content to fit*");
        return m_result;
    }

    auto fitted = std::make_unique<lcad::TextEntity>(original);
    const lcad::Point2D dir = pt - m_start;
    fitted->translate(m_start - original.position());
    fitted->rotate(m_start, std::atan2(dir.y, dir.x) - original.rotation());
    fitted->setWidthFactor(distance / baseWidth);

    m_document.commandStack().execute(
        std::make_unique<lcad::ReplaceEntityCommand>(m_document, m_targetId, std::move(fitted)));
    m_result = QStringLiteral("*Text fitted*");
    return m_result;
}

void TextFitCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> TextFitCommand::previewSegments() const {
    if (m_stage == 1 && m_hasPreview) return {{m_start, m_previewPoint}};
    return {};
}
