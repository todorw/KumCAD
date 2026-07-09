#include "commands/TrimCommand.h"

#include "core/document/Commands.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Intersect.h"
#include "core/geometry/Line.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kTwoPi = 2.0 * M_PI;
constexpr double kParamEps = 1e-6;

double normalizeAngle(double angle) {
    angle = std::fmod(angle, kTwoPi);
    if (angle < 0) angle += kTwoPi;
    return angle;
}

const QString kPrompt = QStringLiteral("Select object to trim (click the part to remove) or [Enter to finish]:");

} // namespace

QString TrimCommand::start() {
    const QString edges = m_edgeIds.empty() ? QStringLiteral("all entities")
                                            : QStringLiteral("%1 selected").arg(static_cast<int>(m_edgeIds.size()));
    return QStringLiteral("TRIM  Cutting edges: %1\n%2").arg(edges, kPrompt);
}

lcad::Entity* TrimCommand::pickTarget(const lcad::Point2D& pt) const {
    lcad::Entity* best = nullptr;
    double bestDist = m_pickTolerance;
    for (lcad::Entity* e : m_document.entities()) {
        const lcad::Layer* layer = m_document.findLayer(e->layer());
        if (layer && (!layer->visible || layer->locked)) continue;
        const lcad::EntityType t = e->type();
        if (t != lcad::EntityType::Line && t != lcad::EntityType::Circle && t != lcad::EntityType::Arc) continue;
        const double d = e->distanceTo(pt);
        if (d <= bestDist) {
            bestDist = d;
            best = e;
        }
    }
    return best;
}

std::vector<lcad::Entity*> TrimCommand::cuttingEdges(lcad::EntityId excludeId) const {
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

std::optional<QString> TrimCommand::onPoint(const lcad::Point2D& pt) {
    lcad::Entity* target = pickTarget(pt);
    if (!target) return QStringLiteral("*No trimmable entity there*\n") + kPrompt;

    QString note;
    switch (target->type()) {
    case lcad::EntityType::Line:
        note = trimLine(*target, pt);
        break;
    case lcad::EntityType::Circle:
        note = trimCircle(*target, pt);
        break;
    case lcad::EntityType::Arc:
        note = trimArc(*target, pt);
        break;
    default:
        break;
    }
    return note + QStringLiteral("\n") + kPrompt;
}

QString TrimCommand::trimLine(lcad::Entity& target, const lcad::Point2D& pt) {
    const auto& line = static_cast<const lcad::LineEntity&>(target);
    const lcad::Point2D a = line.start();
    const lcad::Point2D b = line.end();
    const lcad::Point2D d = b - a;
    const double lenSq = d.dot(d);
    if (lenSq < 1e-12) return QStringLiteral("*Cannot trim a zero-length line*");

    std::vector<double> params;
    for (const lcad::Entity* edge : cuttingEdges(target.id())) {
        for (const lcad::Point2D& p : lcad::intersectEntities(target, *edge)) {
            const double t = (p - a).dot(d) / lenSq;
            if (t > kParamEps && t < 1.0 - kParamEps) params.push_back(t);
        }
    }
    if (params.empty()) return QStringLiteral("*No cutting edge intersects that line*");

    const double tc = std::clamp((pt - a).dot(d) / lenSq, 0.0, 1.0);
    double lower = 0.0;
    double upper = 1.0;
    for (double t : params) {
        if (t <= tc && t > lower) lower = t;
        if (t > tc && t < upper) upper = t;
    }

    auto batch = std::make_unique<lcad::BatchCommand>("Trim");
    batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, target.id()));
    if (lower > kParamEps) {
        batch->add(std::make_unique<lcad::AddEntityCommand>(
            m_document, std::make_unique<lcad::LineEntity>(m_document.reserveEntityId(), target.layer(), a, a + d * lower)));
    }
    if (upper < 1.0 - kParamEps) {
        batch->add(std::make_unique<lcad::AddEntityCommand>(
            m_document, std::make_unique<lcad::LineEntity>(m_document.reserveEntityId(), target.layer(), a + d * upper, b)));
    }
    m_document.commandStack().execute(std::move(batch));
    return QStringLiteral("*Trimmed*");
}

QString TrimCommand::trimCircle(lcad::Entity& target, const lcad::Point2D& pt) {
    const auto& circle = static_cast<const lcad::CircleEntity&>(target);

    std::vector<double> angles;
    for (const lcad::Entity* edge : cuttingEdges(target.id())) {
        for (const lcad::Point2D& p : lcad::intersectEntities(target, *edge)) {
            angles.push_back(normalizeAngle(std::atan2(p.y - circle.center().y, p.x - circle.center().x)));
        }
    }
    std::sort(angles.begin(), angles.end());
    angles.erase(std::unique(angles.begin(), angles.end(),
                             [](double x, double y) { return std::abs(x - y) < kParamEps; }),
                 angles.end());
    if (angles.size() < 2) return QStringLiteral("*A circle needs at least two intersections to trim*");

    const double ac = normalizeAngle(std::atan2(pt.y - circle.center().y, pt.x - circle.center().x));
    // Find the wedge [lower, upper) that contains the click, wrapping around 0.
    double lower = angles.back();
    double upper = angles.front();
    for (std::size_t i = 0; i < angles.size(); ++i) {
        if (angles[i] <= ac) lower = angles[i];
        if (angles[angles.size() - 1 - i] > ac) upper = angles[angles.size() - 1 - i];
    }

    auto batch = std::make_unique<lcad::BatchCommand>("Trim");
    batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, target.id()));
    batch->add(std::make_unique<lcad::AddEntityCommand>(
        m_document, std::make_unique<lcad::ArcEntity>(m_document.reserveEntityId(), target.layer(), circle.center(),
                                                      circle.radius(), upper, lower)));
    m_document.commandStack().execute(std::move(batch));
    return QStringLiteral("*Trimmed*");
}

QString TrimCommand::trimArc(lcad::Entity& target, const lcad::Point2D& pt) {
    const auto& arc = static_cast<const lcad::ArcEntity&>(target);
    const double start = arc.startAngle();
    double sweep = normalizeAngle(arc.endAngle()) - normalizeAngle(start);
    if (sweep <= 0) sweep += kTwoPi;

    // Work in "sweep parameter" space: s = CCW angle from the start, in (0, sweep).
    std::vector<double> params;
    for (const lcad::Entity* edge : cuttingEdges(target.id())) {
        for (const lcad::Point2D& p : lcad::intersectEntities(target, *edge)) {
            const double ang = std::atan2(p.y - arc.center().y, p.x - arc.center().x);
            const double s = normalizeAngle(ang - start);
            if (s > kParamEps && s < sweep - kParamEps) params.push_back(s);
        }
    }
    if (params.empty()) return QStringLiteral("*No cutting edge intersects that arc*");

    double sc = normalizeAngle(std::atan2(pt.y - arc.center().y, pt.x - arc.center().x) - start);
    sc = std::clamp(sc, 0.0, sweep);
    double lower = 0.0;
    double upper = sweep;
    for (double s : params) {
        if (s <= sc && s > lower) lower = s;
        if (s > sc && s < upper) upper = s;
    }

    auto batch = std::make_unique<lcad::BatchCommand>("Trim");
    batch->add(std::make_unique<lcad::DeleteEntityCommand>(m_document, target.id()));
    if (lower > kParamEps) {
        batch->add(std::make_unique<lcad::AddEntityCommand>(
            m_document, std::make_unique<lcad::ArcEntity>(m_document.reserveEntityId(), target.layer(), arc.center(),
                                                          arc.radius(), start, start + lower)));
    }
    if (upper < sweep - kParamEps) {
        batch->add(std::make_unique<lcad::AddEntityCommand>(
            m_document, std::make_unique<lcad::ArcEntity>(m_document.reserveEntityId(), target.layer(), arc.center(),
                                                          arc.radius(), start + upper, start + sweep)));
    }
    m_document.commandStack().execute(std::move(batch));
    return QStringLiteral("*Trimmed*");
}
