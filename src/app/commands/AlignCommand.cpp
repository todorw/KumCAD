#include "commands/AlignCommand.h"

#include "core/document/Commands.h"

#include <cmath>

QString AlignCommand::start() {
    return QStringLiteral("ALIGN  %1 found\nSpecify first source point:").arg(static_cast<int>(m_ids.size()));
}

std::optional<QString> AlignCommand::onPoint(const lcad::Point2D& pt) {
    switch (m_stage) {
    case Stage::Source1:
        m_s1 = pt;
        m_stage = Stage::Dest1;
        return QStringLiteral("Specify first destination point:");
    case Stage::Dest1:
        m_d1 = pt;
        m_stage = Stage::Source2;
        return QStringLiteral("Specify second source point or [Enter to move only]:");
    case Stage::Source2:
        m_s2 = pt;
        m_stage = Stage::Dest2;
        return QStringLiteral("Specify second destination point:");
    case Stage::Dest2:
        m_d2 = pt;
        m_stage = Stage::ScaleQuery;
        return QStringLiteral("Scale objects based on alignment points? [Yes/No] <No>:");
    case Stage::ScaleQuery:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<QString> AlignCommand::onOption(const QString& option) {
    if (m_stage != Stage::ScaleQuery) return std::nullopt;
    const QString opt = option.toUpper();
    if (opt == QLatin1String("Y") || opt == QLatin1String("YES")) return apply(true);
    if (opt == QLatin1String("N") || opt == QLatin1String("NO")) return apply(false);
    return std::nullopt;
}

bool AlignCommand::requestFinish() {
    if (m_stage == Stage::Source2) {
        // One pair only: plain move.
        m_document.commandStack().execute(
            std::make_unique<lcad::TranslateEntitiesCommand>(m_document, m_ids, m_d1 - m_s1));
        m_resultMessage = QStringLiteral("*Aligned (move only)*");
        m_finished = true;
        return true;
    }
    if (m_stage == Stage::ScaleQuery) {
        m_resultMessage = apply(false);
        m_finished = true;
        return true;
    }
    return false;
}

QString AlignCommand::apply(bool scaleToFit) {
    m_finished = true;

    const lcad::Point2D delta = m_d1 - m_s1;
    const lcad::Point2D srcVec = m_s2 - m_s1;
    const lcad::Point2D dstVec = m_d2 - m_d1;
    const double srcLen = srcVec.length();
    const double dstLen = dstVec.length();
    if (srcLen < 1e-9 || dstLen < 1e-9) return QStringLiteral("*Alignment points are coincident; nothing done*");

    const double angle = std::atan2(dstVec.y, dstVec.x) - std::atan2(srcVec.y, srcVec.x);

    auto batch = std::make_unique<lcad::BatchCommand>("Align");
    batch->add(std::make_unique<lcad::TranslateEntitiesCommand>(m_document, m_ids, delta));
    batch->add(std::make_unique<lcad::RotateEntitiesCommand>(m_document, m_ids, m_d1, angle));
    if (scaleToFit) {
        batch->add(std::make_unique<lcad::ScaleEntitiesCommand>(m_document, m_ids, m_d1, dstLen / srcLen));
    }
    m_document.commandStack().execute(std::move(batch));
    return scaleToFit ? QStringLiteral("*Aligned (with scale %1)*").arg(dstLen / srcLen, 0, 'f', 4)
                      : QStringLiteral("*Aligned*");
}

void AlignCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> AlignCommand::previewSegments() const {
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    if (m_stage == Stage::Dest1 && m_hasPreview) segs.push_back({m_s1, m_previewPoint});
    if (m_stage == Stage::Dest2 && m_hasPreview) segs.push_back({m_s2, m_previewPoint});
    if (m_stage >= Stage::Source2) segs.push_back({m_s1, m_d1});
    if (m_stage == Stage::ScaleQuery) segs.push_back({m_s2, m_d2});
    return segs;
}
