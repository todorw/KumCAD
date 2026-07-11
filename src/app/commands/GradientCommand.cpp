#include "commands/GradientCommand.h"

#include "commands/HatchBoundary.h"
#include "core/document/Commands.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Polyline.h"
#include "core/io/DxfColors.h"

#include <cmath>

QString GradientCommand::start() {
    if (m_stage == Stage::BoundaryPick) return QStringLiteral("GRADIENT  Pick internal point:");
    return QStringLiteral("GRADIENT  Enter first color [ACI 1-255] <5>:");
}

std::optional<QString> GradientCommand::onPoint(const lcad::Point2D& pt) {
    if (m_stage != Stage::BoundaryPick) return std::nullopt;
    if (auto boundary = pickHatchBoundary(m_document, pt)) {
        m_pickedBoundaries.push_back(std::move(*boundary));
        return QStringLiteral("*%1 boundary(ies) found* Pick internal point (Enter to finish):")
            .arg(m_pickedBoundaries.size());
    }
    return QStringLiteral("*Valid boundary not found there*\nPick internal point:");
}

std::optional<QString> GradientCommand::onScalar(double value) {
    switch (m_stage) {
    case Stage::Color1:
        if (value < 1 || value > 255) return QStringLiteral("*Color must be an ACI number 1-255*");
        m_aci1 = static_cast<int>(value);
        m_stage = Stage::Color2;
        return QStringLiteral("Enter second color [ACI 1-255] <150>:");
    case Stage::Color2:
        if (value < 1 || value > 255) return QStringLiteral("*Color must be an ACI number 1-255*");
        m_aci2 = static_cast<int>(value);
        m_stage = Stage::Angle;
        return QStringLiteral("Specify gradient angle <0>:");
    case Stage::Angle:
        m_angleDeg = value;
        commit();
        m_finished = true;
        return m_result;
    default:
        return std::nullopt;
    }
}

bool GradientCommand::requestFinish() {
    if (m_stage == Stage::BoundaryPick) {
        if (m_pickedBoundaries.empty()) {
            m_finished = true;
            return false;
        }
        m_stage = Stage::Color1;
        return true; // not finished yet -- resultMessage() gives the color prompt
    }
    commit();
    m_finished = true;
    return true;
}

void GradientCommand::commit() {
    auto batch = std::make_unique<lcad::BatchCommand>("Gradient");
    int made = 0;
    int skipped = 0;
    const lcad::Color color1 = lcad::aciToColor(m_aci1);
    const lcad::Color color2 = lcad::aciToColor(m_aci2);
    const double angleRad = m_angleDeg * M_PI / 180.0;

    auto addGradient = [&](std::vector<lcad::Point2D> vertices, lcad::LayerId layer) {
        auto hatch = std::make_unique<lcad::HatchEntity>(m_document.reserveEntityId(), layer, std::move(vertices),
                                                          lcad::HatchPattern::Solid, 1.0, angleRad);
        hatch->setColorOverride(color1);
        hatch->setGradientColor2(color2);
        batch->add(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(hatch)));
        ++made;
    };

    for (lcad::EntityId id : m_ids) {
        const lcad::Entity* e = m_document.findEntity(id);
        if (e && e->type() == lcad::EntityType::Polyline) {
            const auto& pl = static_cast<const lcad::PolylineEntity&>(*e);
            if (pl.closed() && pl.vertices().size() >= 3) {
                addGradient(pl.flattenedVertices(), e->layer());
                continue;
            }
        }
        ++skipped;
    }
    for (auto& boundary : m_pickedBoundaries) addGradient(std::move(boundary), m_document.currentLayer());

    if (!batch->empty()) m_document.commandStack().execute(std::move(batch));
    m_result = skipped > 0
                   ? QStringLiteral("*%1 filled, %2 skipped (only closed polylines can be filled)*").arg(made).arg(skipped)
                   : QStringLiteral("*%1 filled*").arg(made);
}
