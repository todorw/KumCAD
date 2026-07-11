#include "commands/BreakCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/ModifyOps.h"

namespace {

lcad::Entity* pickBreakTarget(lcad::Document& document, const lcad::Point2D& pt, double tolerance) {
    lcad::Entity* best = nullptr;
    double bestDist = tolerance;
    for (lcad::Entity* e : document.entities()) {
        const lcad::Layer* layer = document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue;
        const lcad::EntityType t = e->type();
        if (t != lcad::EntityType::Line && t != lcad::EntityType::Arc && t != lcad::EntityType::Circle &&
            t != lcad::EntityType::Polyline) {
            continue;
        }
        const double d = e->distanceTo(pt);
        if (d <= bestDist) {
            bestDist = d;
            best = e;
        }
    }
    return best;
}

} // namespace

QString BreakCommand::start() {
    return QStringLiteral("BREAK  Select object (the pick point is the first break point):");
}

std::optional<QString> BreakCommand::onOption(const QString& option) {
    const QString opt = option.toUpper();
    if (m_stage == Stage::SecondPoint && (opt == QLatin1String("F") || opt == QLatin1String("FIRST"))) {
        m_stage = Stage::FirstPoint;
        return QStringLiteral("Specify first break point:");
    }
    if (m_stage == Stage::SecondPoint && opt == QLatin1String("@")) {
        return applyBreak(m_firstPoint);
    }
    return std::nullopt;
}

std::optional<QString> BreakCommand::onPoint(const lcad::Point2D& pt) {
    switch (m_stage) {
    case Stage::SelectObject: {
        lcad::Entity* target = pickBreakTarget(m_document, pt, m_pickTolerance);
        if (!target) return QStringLiteral("*No breakable entity there*\nSelect object:");
        m_targetId = target->id();
        m_firstPoint = pt;
        m_stage = Stage::SecondPoint;
        return QStringLiteral("Specify second break point or [First/@ to break at point]:");
    }
    case Stage::FirstPoint:
        m_firstPoint = pt;
        m_stage = Stage::SecondPoint;
        return QStringLiteral("Specify second break point or [@ to break at point]:");
    case Stage::SecondPoint:
        return applyBreak(pt);
    }
    return std::nullopt;
}

QString BreakCommand::applyBreak(const lcad::Point2D& secondPt) {
    const lcad::Entity* target = m_document.findEntity(m_targetId);
    if (!target) {
        m_finished = true;
        return QStringLiteral("*Entity vanished*");
    }

    lcad::BreakResult broken =
        lcad::breakEntity(*target, m_firstPoint, secondPt, [this]() { return m_document.reserveEntityId(); });
    m_finished = true;
    if (!broken.ok) return QStringLiteral("*Cannot break that entity (closed or arc-segment polylines aren't supported)*");

    auto batch = std::make_unique<lcad::BatchCommand>("Break");
    batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, m_targetId));
    const int pieces = static_cast<int>(broken.pieces.size());
    for (auto& piece : broken.pieces) {
        batch->add(std::make_unique<lcad::AddEntityCommand>(m_document, std::move(piece)));
    }
    m_document.commandStack().execute(std::move(batch));
    return QStringLiteral("*Broken into %1 piece(s)*").arg(pieces);
}
