#include "commands/MLeaderCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/MLeader.h"
#include "core/geometry/MText.h"

QString MLeaderCommand::start() {
    return QStringLiteral("MLEADER  Specify leader arrowhead location:");
}

std::optional<QString> MLeaderCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::Points) return std::nullopt;
    m_points.push_back(pt);
    if (m_points.size() == 1) return QStringLiteral("Specify leader landing location:");
    return QStringLiteral("Specify next point or [Enter for annotation]:");
}

bool MLeaderCommand::requestFinish() {
    if (m_stage == Stage::Points) {
        if (m_points.size() < 2) {
            m_finished = true;
            return false;
        }
        m_stage = Stage::Text;
        return true;
    }
    commit();
    m_finished = true;
    return true;
}

std::optional<QString> MLeaderCommand::onText(const QString& text) {
    if (!text.isEmpty()) {
        m_lines << text;
        return QStringLiteral("Enter next annotation line (empty line to finish):");
    }
    commit();
    m_finished = true;
    return std::nullopt;
}

void MLeaderCommand::commit() {
    if (m_points.size() < 2) return;
    const lcad::MLeaderStyle& style = m_document.mleaderStyle();

    const lcad::Point2D landing = m_points.back();
    std::vector<lcad::Point2D> legPoints(m_points.begin(), m_points.end() - 1);

    auto batch = std::make_unique<lcad::BatchCommand>("MLeader");
    batch->add(std::make_unique<lcad::AddEntityCommand>(
        m_document, std::make_unique<lcad::MLeaderEntity>(m_document.reserveEntityId(), m_document.currentLayer(),
                                                           std::vector<std::vector<lcad::Point2D>>{legPoints},
                                                           landing, style.arrowSize)));

    if (!m_lines.isEmpty()) {
        const bool leftward = m_points.size() >= 2 && m_points[m_points.size() - 2].x > landing.x;
        const double gap = style.landingGap * style.textHeight;
        const QString content = m_lines.join(QLatin1Char('\n'));
        auto mtext = std::make_unique<lcad::MTextEntity>(
            m_document.reserveEntityId(), m_document.currentLayer(),
            lcad::Point2D(landing.x + (leftward ? -gap : gap), landing.y + 0.8 * style.textHeight),
            content.toStdString(), style.textHeight);
        if (leftward) mtext->translate(lcad::Point2D(-mtext->blockWidth(), 0));
        batch->add(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(mtext)));
    }
    m_document.commandStack().execute(std::move(batch));
}

void MLeaderCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> MLeaderCommand::previewSegments() const {
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    for (std::size_t i = 0; i + 1 < m_points.size(); ++i) segs.emplace_back(m_points[i], m_points[i + 1]);
    if (m_stage == Stage::Points && !m_points.empty() && m_hasPreview) {
        segs.emplace_back(m_points.back(), m_previewPoint);
    }
    return segs;
}

std::optional<QString> MLeaderAddLeaderCommand::onPoint(const lcad::Point2D& pt) {
    m_points.push_back(pt);
    return QStringLiteral("Specify next point or [Enter to connect to the landing]:");
}

bool MLeaderAddLeaderCommand::requestFinish() {
    m_finished = true;
    if (m_points.empty()) return false;
    const lcad::Entity* e = m_document.findEntity(m_mleaderId);
    if (!e || e->type() != lcad::EntityType::MLeader) return false;
    const auto* existing = static_cast<const lcad::MLeaderEntity*>(e);

    auto updated = std::make_unique<lcad::MLeaderEntity>(m_mleaderId, existing->layer(), existing->legs(),
                                                          existing->landing(), existing->arrowSize());
    updated->addLeg(m_points);
    m_document.commandStack().execute(
        std::make_unique<lcad::ReplaceEntityCommand>(m_document, m_mleaderId, std::move(updated)));
    return true;
}

void MLeaderAddLeaderCommand::onPreviewPoint(const lcad::Point2D& pt) {
    m_previewPoint = pt;
    m_hasPreview = true;
}

std::vector<std::pair<lcad::Point2D, lcad::Point2D>> MLeaderAddLeaderCommand::previewSegments() const {
    std::vector<std::pair<lcad::Point2D, lcad::Point2D>> segs;
    for (std::size_t i = 0; i + 1 < m_points.size(); ++i) segs.emplace_back(m_points[i], m_points[i + 1]);
    if (!m_points.empty() && m_hasPreview) segs.emplace_back(m_points.back(), m_previewPoint);
    return segs;
}
