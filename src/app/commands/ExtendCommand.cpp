#include "commands/ExtendCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Intersect.h"
#include "core/geometry/Line.h"

#include <cmath>
#include <limits>

namespace {

constexpr double kTwoPi = 2.0 * M_PI;
constexpr double kEps = 1e-6;

double normalizeAngle(double angle) {
    angle = std::fmod(angle, kTwoPi);
    if (angle < 0) angle += kTwoPi;
    return angle;
}

const QString kPrompt = QStringLiteral("Select object to extend (click near the end to lengthen) or [Enter to finish]:");

} // namespace

QString ExtendCommand::start() {
    const QString edges = m_edgeIds.empty() ? QStringLiteral("all entities")
                                            : QStringLiteral("%1 selected").arg(static_cast<int>(m_edgeIds.size()));
    return QStringLiteral("EXTEND  Boundary edges: %1\n%2").arg(edges, kPrompt);
}

std::vector<lcad::Entity*> ExtendCommand::boundaryEdges(lcad::EntityId excludeId) const {
    std::vector<lcad::Entity*> edges;
    if (m_edgeIds.empty()) {
        for (lcad::Entity* e : m_document.entities()) {
            if (e->id() != excludeId) edges.push_back(e);
        }
    } else {
        for (lcad::EntityId id : m_edgeIds) {
            if (id == excludeId) continue;
            if (lcad::Entity* e = m_document.findEntity(id)) edges.push_back(e);
        }
    }
    return edges;
}

std::optional<QString> ExtendCommand::onPoint(const lcad::Point2D& pt) {
    lcad::Entity* target = nullptr;
    double bestDist = m_pickTolerance;
    for (lcad::Entity* e : m_document.entities()) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue;
        if (e->type() != lcad::EntityType::Line && e->type() != lcad::EntityType::Arc) continue;
        const double d = e->distanceTo(pt);
        if (d <= bestDist) {
            bestDist = d;
            target = e;
        }
    }
    if (!target) return QStringLiteral("*No extendable entity there (lines and arcs only)*\n") + kPrompt;

    const QString note = target->type() == lcad::EntityType::Line ? extendLine(*target, pt) : extendArc(*target, pt);
    return note + QStringLiteral("\n") + kPrompt;
}

QString ExtendCommand::extendLine(lcad::Entity& target, const lcad::Point2D& pt) {
    const auto& line = static_cast<const lcad::LineEntity&>(target);
    const lcad::Point2D a = line.start();
    const lcad::Point2D b = line.end();
    const lcad::Point2D d = b - a;
    const double lenSq = d.dot(d);
    if (lenSq < 1e-12) return QStringLiteral("*Cannot extend a zero-length line*");

    // Which end was clicked?
    const bool endClicked = pt.distanceTo(b) <= pt.distanceTo(a);

    double bestT = endClicked ? std::numeric_limits<double>::max() : std::numeric_limits<double>::lowest();
    bool found = false;
    for (const lcad::Entity* edge : boundaryEdges(target.id())) {
        for (const lcad::Point2D& p : lcad::intersectInfiniteLineEntity(a, b, *edge)) {
            const double t = (p - a).dot(d) / lenSq;
            if (endClicked && t > 1.0 + kEps && t < bestT) {
                bestT = t;
                found = true;
            } else if (!endClicked && t < -kEps && t > bestT) {
                bestT = t;
                found = true;
            }
        }
    }
    if (!found) return QStringLiteral("*No boundary edge beyond that end*");

    const lcad::Point2D newPos = a + d * bestT;
    const std::size_t gripIndex = endClicked ? 1 : 0; // line grips: 0 = start, 1 = end
    m_document.commandStack().execute(std::make_unique<lcad::MoveGripCommand>(
        m_document, target.id(), gripIndex, endClicked ? b : a, newPos));
    return QStringLiteral("*Extended*");
}

QString ExtendCommand::extendArc(lcad::Entity& target, const lcad::Point2D& pt) {
    const auto& arc = static_cast<const lcad::ArcEntity&>(target);
    const double start = normalizeAngle(arc.startAngle());
    const double end = normalizeAngle(arc.endAngle());
    double sweep = end - start;
    if (sweep <= 0) sweep += kTwoPi;
    const double room = kTwoPi - sweep; // how far either end can grow before hitting the other

    const bool endClicked = pt.distanceTo(arc.endPoint()) <= pt.distanceTo(arc.startPoint());

    // Intersections of the arc's full circle with the boundaries, as growth
    // deltas beyond the clicked end.
    const lcad::CircleEntity fullCircle(0, 0, arc.center(), arc.radius());
    double bestDelta = std::numeric_limits<double>::max();
    for (const lcad::Entity* edge : boundaryEdges(target.id())) {
        for (const lcad::Point2D& p : lcad::intersectEntities(fullCircle, *edge)) {
            const double ang = normalizeAngle(std::atan2(p.y - arc.center().y, p.x - arc.center().x));
            const double delta = endClicked ? normalizeAngle(ang - end) : normalizeAngle(start - ang);
            if (delta > kEps && delta < room - kEps && delta < bestDelta) bestDelta = delta;
        }
    }
    if (bestDelta == std::numeric_limits<double>::max()) return QStringLiteral("*No boundary edge beyond that end*");

    const double newAngle = endClicked ? end + bestDelta : start - bestDelta;
    const lcad::Point2D newPos(arc.center().x + arc.radius() * std::cos(newAngle),
                               arc.center().y + arc.radius() * std::sin(newAngle));
    const std::size_t gripIndex = endClicked ? 1 : 0; // arc grips: 0 = start, 1 = end
    m_document.commandStack().execute(std::make_unique<lcad::MoveGripCommand>(
        m_document, target.id(), gripIndex, endClicked ? arc.endPoint() : arc.startPoint(), newPos));
    return QStringLiteral("*Extended*");
}
