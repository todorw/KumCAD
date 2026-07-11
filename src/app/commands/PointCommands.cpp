#include "commands/PointCommands.h"

#include "core/document/Commands.h"
#include "core/geometry/ModifyOps.h"
#include "core/geometry/PointEnt.h"

std::optional<QString> PointCommand::onPoint(const lcad::Point2D& pt) {
    m_document.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(
        m_document, std::make_unique<lcad::PointEntity>(m_document.reserveEntityId(), m_document.currentLayer(), pt)));
    return QStringLiteral("Specify a point (Enter to finish):");
}

std::optional<QString> PdModeCommand::onText(const QString& text) {
    const QString trimmed = text.trimmed();
    if (m_stage == Stage::Mode) {
        if (!trimmed.isEmpty()) {
            bool ok = false;
            const int v = trimmed.toInt(&ok);
            if (!ok || v < 0) return QStringLiteral("*Invalid* Enter mode <%1>:").arg(m_document.pointMode());
            m_document.setPointMode(v);
        }
        m_stage = Stage::Size;
        return QStringLiteral("Marker size in drawing units <%1>:").arg(m_document.pointSize());
    }
    if (!trimmed.isEmpty()) {
        bool ok = false;
        const double v = trimmed.toDouble(&ok);
        if (!ok || v <= 0) return QStringLiteral("*Invalid* Marker size <%1>:").arg(m_document.pointSize());
        m_document.setPointSize(v);
    }
    m_finished = true;
    return QStringLiteral("*Point style: mode %1, size %2*").arg(m_document.pointMode()).arg(m_document.pointSize());
}

QString DivideCommand::start() {
    return m_measure ? QStringLiteral("MEASURE  Select object to measure:")
                     : QStringLiteral("DIVIDE  Select object to divide:");
}

std::optional<QString> DivideCommand::onPoint(const lcad::Point2D& pt) {
    if (m_targetId != 0) return std::nullopt; // waiting for the number, not a point
    lcad::Entity* best = nullptr;
    double bestDist = m_pickTolerance;
    for (lcad::Entity* e : m_document.entities()) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
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
    if (!best) return QStringLiteral("*No dividable entity there*\nSelect object:");
    m_targetId = best->id();
    return m_measure ? QStringLiteral("Enter segment length:") : QStringLiteral("Enter number of segments:");
}

std::optional<QString> DivideCommand::onScalar(double value) {
    if (m_targetId == 0) return std::nullopt;
    const lcad::Entity* target = m_document.findEntity(m_targetId);
    if (!target) {
        m_finished = true;
        return QStringLiteral("*Entity vanished*");
    }

    std::vector<lcad::Point2D> points;
    if (m_measure) {
        if (value <= 1e-9) return QStringLiteral("*Length must be positive*\nEnter segment length:");
        points = lcad::measureEntity(*target, value);
    } else {
        const int n = static_cast<int>(value);
        if (n < 2 || n > 10000) return QStringLiteral("*Need 2..10000 segments*\nEnter number of segments:");
        points = lcad::divideEntity(*target, n);
    }
    m_finished = true;
    if (points.empty()) return QStringLiteral("*Nothing to place*");

    auto batch = std::make_unique<lcad::BatchCommand>(m_measure ? "Measure" : "Divide");
    for (const lcad::Point2D& p : points) {
        batch->add(std::make_unique<lcad::AddEntityCommand>(
            m_document,
            std::make_unique<lcad::PointEntity>(m_document.reserveEntityId(), m_document.currentLayer(), p)));
    }
    m_document.commandStack().execute(std::move(batch));
    return QStringLiteral("*%1 points placed (PDMODE styles them)*").arg(points.size());
}
