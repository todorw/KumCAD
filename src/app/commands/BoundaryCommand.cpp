#include "commands/BoundaryCommand.h"

#include "commands/HatchBoundary.h"
#include "core/document/Commands.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Region.h"

QString BoundaryCommand::start() { return QStringLiteral("BOUNDARY  Pick internal point:"); }

std::optional<QString> BoundaryCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::BoundaryPick) return std::nullopt;
    if (auto boundary = pickHatchBoundary(m_document, pt)) {
        m_pickedBoundaries.push_back(std::move(*boundary));
        return QStringLiteral("*%1 boundary(ies) found* Pick internal point (Enter to finish):")
            .arg(m_pickedBoundaries.size());
    }
    return QStringLiteral("*Valid boundary not found there*\nPick internal point:");
}

std::optional<QString> BoundaryCommand::onOption(const QString& option) {
    if (m_stage != Stage::ObjectType) return std::nullopt;
    const QString lower = option.trimmed().toLower();
    if (lower == QStringLiteral("region") || lower == QStringLiteral("r")) {
        m_objectType = ObjectType::Region;
    } else if (lower == QStringLiteral("polyline") || lower == QStringLiteral("p")) {
        m_objectType = ObjectType::Polyline;
    } else {
        return std::nullopt;
    }
    commit();
    m_finished = true;
    return m_result;
}

bool BoundaryCommand::requestFinish() {
    if (m_stage == Stage::BoundaryPick) {
        if (m_pickedBoundaries.empty()) {
            m_finished = true;
            return false;
        }
        m_stage = Stage::ObjectType;
        return true; // not finished yet -- resultMessage() gives the object-type prompt
    }
    // Enter: accept the Polyline default.
    commit();
    m_finished = true;
    return true;
}

void BoundaryCommand::commit() {
    auto batch = std::make_unique<lcad::BatchCommand>("Boundary");
    for (auto& boundary : m_pickedBoundaries) {
        if (m_objectType == ObjectType::Region) {
            batch->add(std::make_unique<lcad::AddEntityCommand>(
                m_document, std::make_unique<lcad::RegionEntity>(
                                m_document.reserveEntityId(), m_document.currentLayer(),
                                std::vector<lcad::RegionLoop>{lcad::RegionLoop{std::move(boundary)}})));
        } else {
            batch->add(std::make_unique<lcad::AddEntityCommand>(
                m_document, std::make_unique<lcad::PolylineEntity>(m_document.reserveEntityId(),
                                                                    m_document.currentLayer(), std::move(boundary), true)));
        }
    }
    const int made = static_cast<int>(m_pickedBoundaries.size());
    if (!batch->empty()) m_document.commandStack().execute(std::move(batch));
    m_result = QStringLiteral("*%1 boundary(ies) created*").arg(made);
}
