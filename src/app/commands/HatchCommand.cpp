#include "commands/HatchCommand.h"

#include "commands/HatchBoundary.h"
#include "core/document/Commands.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Polyline.h"

#include <cmath>

QString HatchCommand::start() {
    if (m_stage == Stage::BoundaryPick) return QStringLiteral("HATCH  Pick internal point:");
    return QStringLiteral("HATCH  Enter pattern [SOLID/ANSI31/ANSI32/ANSI33/ANSI37] <SOLID>:");
}

std::optional<QString> HatchCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::BoundaryPick) return std::nullopt;
    if (auto boundary = pickHatchBoundary(m_document, pt)) {
        m_pickedBoundaries.push_back(std::move(*boundary));
        return QStringLiteral("*%1 boundary(ies) found* Pick internal point (Enter to finish):")
            .arg(m_pickedBoundaries.size());
    }
    return QStringLiteral("*Valid boundary not found there*\nPick internal point:");
}

std::optional<QString> HatchCommand::onOption(const QString& option) {
    if (m_stage != Stage::Pattern) return std::nullopt;
    const auto pattern = lcad::hatchPatternFromName(option.toStdString());
    if (!pattern) return std::nullopt;
    m_pattern = *pattern;
    if (m_pattern == lcad::HatchPattern::Solid) {
        commit();
        m_finished = true;
        return m_result;
    }
    m_stage = Stage::Scale;
    return QStringLiteral("Specify pattern scale <1.0>:");
}

std::optional<QString> HatchCommand::onScalar(double value) {
    if (m_stage == Stage::Scale) {
        if (value <= 0) return QStringLiteral("*Scale must be positive*");
        m_scale = value;
        m_stage = Stage::Angle;
        return QStringLiteral("Specify pattern angle <0>:");
    }
    if (m_stage == Stage::Angle) {
        m_angleDeg = value;
        commit();
        m_finished = true;
        return m_result;
    }
    return std::nullopt;
}

bool HatchCommand::requestFinish() {
    if (m_stage == Stage::BoundaryPick) {
        if (m_pickedBoundaries.empty()) {
            m_finished = true;
            return false;
        }
        m_stage = Stage::Pattern;
        return true; // not finished yet -- resultMessage() gives the pattern prompt
    }
    // Enter: accept the defaults for whatever wasn't specified yet.
    commit();
    m_finished = true;
    return true;
}

void HatchCommand::commit() {
    auto batch = std::make_unique<lcad::BatchCommand>("Hatch");
    int made = 0;
    int skipped = 0;
    for (lcad::EntityId id : m_ids) {
        const lcad::Entity* e = m_document.findEntity(id);
        if (e && e->type() == lcad::EntityType::Polyline) {
            const auto& pl = static_cast<const lcad::PolylineEntity&>(*e);
            if (pl.closed() && pl.vertices().size() >= 3) {
                batch->add(std::make_unique<lcad::AddEntityCommand>(
                    m_document,
                    std::make_unique<lcad::HatchEntity>(m_document.reserveEntityId(), e->layer(),
                                                        pl.flattenedVertices(), m_pattern, m_scale,
                                                        m_angleDeg * M_PI / 180.0)));
                ++made;
                continue;
            }
        }
        ++skipped;
    }
    for (auto& boundary : m_pickedBoundaries) {
        batch->add(std::make_unique<lcad::AddEntityCommand>(
            m_document, std::make_unique<lcad::HatchEntity>(m_document.reserveEntityId(), m_document.currentLayer(),
                                                             std::move(boundary), m_pattern, m_scale,
                                                             m_angleDeg * M_PI / 180.0)));
        ++made;
    }
    if (!batch->empty()) m_document.commandStack().execute(std::move(batch));
    m_result = skipped > 0
                   ? QStringLiteral("*%1 hatched, %2 skipped (only closed polylines can be hatched)*").arg(made).arg(skipped)
                   : QStringLiteral("*%1 hatched*").arg(made);
}
