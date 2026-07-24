#include "commands/DonutCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/DonutOps.h"
#include "core/geometry/Region.h"

QString DonutCommand::start() {
    return QStringLiteral("DONUT  Specify inside diameter <0.5>:");
}

bool DonutCommand::wantsTextInput() const {
    return m_stage == Stage::InsideDiameter || m_stage == Stage::OutsideDiameter;
}

std::optional<QString> DonutCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();

    if (m_stage == Stage::InsideDiameter) {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const double value = trimmed.toDouble(&ok);
            if (!ok || value < 0.0) return QStringLiteral("*Invalid inside diameter*");
            m_insideDiameter = value;
        }
        m_stage = Stage::OutsideDiameter;
        return QStringLiteral("Specify outside diameter <1.0>:");
    }

    // Stage::OutsideDiameter
    if (!trimmed.isEmpty()) {
        bool ok = false;
        const double value = trimmed.toDouble(&ok);
        if (!ok || value <= 0.0) return QStringLiteral("*Invalid outside diameter*");
        m_outsideDiameter = value;
    }
    m_stage = Stage::Center;
    return QStringLiteral("Specify center of donut (Enter to finish):");
}

std::optional<QString> DonutCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Center) return std::nullopt; // diameters are text-only stages

    const auto loops = lcad::buildDonutLoops(pt, m_insideDiameter / 2.0, m_outsideDiameter / 2.0);
    if (loops.empty()) return QStringLiteral("*Invalid diameters*");

    auto entity = std::make_unique<lcad::RegionEntity>(m_document.reserveEntityId(), m_document.currentLayer(), loops);
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(entity)));
    return QStringLiteral("Specify center of donut (Enter to finish):");
}

void DonutCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> DonutCommand::previewSegments() const {
    if (m_stage != Stage::Center || !m_hasPreview) return {};
    const auto loops = lcad::buildDonutLoops(m_previewPoint, m_insideDiameter / 2.0, m_outsideDiameter / 2.0, 32);
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    for (const lcad::RegionLoop& loop : loops) {
        for (std::size_t i = 0; i < loop.vertices.size(); ++i) {
            segs.emplace_back(loop.vertices[i], loop.vertices[(i + 1) % loop.vertices.size()]);
        }
    }
    return segs;
}
