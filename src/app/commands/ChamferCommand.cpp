#include "commands/ChamferCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/ChamferOps.h"
#include "core/geometry/Line.h"

QString ChamferCommand::start() {
    return QStringLiteral("CHAMFER  Specify chamfer distance or [Enter for sharp corner]:");
}

std::optional<QString> ChamferCommand::onPoint(const lcad::Point2D& pt) {
    (void)pt;
    return std::nullopt; // distance comes typed; points are not meaningful here
}

std::optional<QString> ChamferCommand::onScalar(double value) {
    if (value < 0) return std::nullopt;
    m_result = apply(value);
    m_finished = true;
    return m_result;
}

bool ChamferCommand::requestFinish() {
    if (!m_finished) m_result = apply(0.0);
    m_finished = true;
    return true;
}

QString ChamferCommand::apply(double distance) {
    lcad::Entity* e1 = m_document.findEntity(m_line1);
    lcad::Entity* e2 = m_document.findEntity(m_line2);
    if (!e1 || !e2 || e1->type() != lcad::EntityType::Line || e2->type() != lcad::EntityType::Line) {
        return QStringLiteral("*CHAMFER needs two lines*");
    }
    const auto& l1 = static_cast<const lcad::LineEntity&>(*e1);
    const auto& l2 = static_cast<const lcad::LineEntity&>(*e2);

    const auto geom = lcad::computeChamferGeometry(l1, l2, distance, distance);
    if (!geom) return QStringLiteral("*Cannot chamfer these lines with that distance*");

    const std::size_t grip1 = geom->keepEnd1 ? 0 : 1; // index of the endpoint being moved
    const std::size_t grip2 = geom->keepEnd2 ? 0 : 1;
    const lcad::Point2D near1 = geom->keepEnd1 ? l1.start() : l1.end();
    const lcad::Point2D near2 = geom->keepEnd2 ? l2.start() : l2.end();

    auto batch = std::make_unique<lcad::BatchCommand>("Chamfer");
    batch->add(std::make_unique<lcad::MoveGripCommand>(m_document, m_line1, grip1, near1, geom->trim1));
    batch->add(std::make_unique<lcad::MoveGripCommand>(m_document, m_line2, grip2, near2, geom->trim2));

    if (distance > 1e-9) {
        batch->add(std::make_unique<lcad::AddEntityCommand>(
            m_document, std::make_unique<lcad::LineEntity>(m_document.reserveEntityId(), e1->layer(), geom->trim1,
                                                            geom->trim2)));
    }

    m_document.commandStack().execute(std::move(batch));
    return distance > 1e-9 ? QStringLiteral("*Chamfered with distance %1*").arg(distance)
                           : QStringLiteral("*Corner joined*");
}
