#include "core/geometry/ModifyOps.h"

#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"
#include "core/geometry/MText.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Text.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lcad {

namespace {

constexpr double kTwoPi = 2.0 * M_PI;
constexpr double kEps = 1e-9;

double normalizeAngle(double angle) {
    angle = std::fmod(angle, kTwoPi);
    if (angle < 0) angle += kTwoPi;
    return angle;
}

double arcSweep(const ArcEntity& arc) {
    double sweep = normalizeAngle(arc.endAngle()) - normalizeAngle(arc.startAngle());
    if (sweep <= 0) sweep += kTwoPi;
    return sweep;
}

void copyStyle(const Entity& src, Entity& dst) {
    dst.setColorOverride(src.colorOverride());
    dst.setLinetypeOverride(src.linetypeOverride());
}

// Moves every grip whose current position is inside window by delta. Only
// valid for entities whose grips are all genuine defining points (polylines,
// splines, hatches, leaders, dimensions).
std::unique_ptr<Entity> stretchByGrips(const Entity& e, const BoundingBox& window, const Point2D& delta) {
    std::unique_ptr<Entity> clone = e.clone();
    const auto grips = e.gripPoints();
    bool changed = false;
    for (std::size_t i = 0; i < grips.size(); ++i) {
        if (window.contains(grips[i])) {
            clone->moveGripPoint(i, grips[i] + delta);
            changed = true;
        }
    }
    return changed ? std::move(clone) : nullptr;
}

std::unique_ptr<Entity> translateIfInside(const Entity& e, const Point2D& primary, const BoundingBox& window,
                                          const Point2D& delta) {
    if (!window.contains(primary)) return nullptr;
    std::unique_ptr<Entity> clone = e.clone();
    clone->translate(delta);
    return clone;
}

} // namespace

std::unique_ptr<Entity> stretchedClone(const Entity& e, const BoundingBox& window, const Point2D& delta) {
    switch (e.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(e);
        const bool startIn = window.contains(line.start());
        const bool endIn = window.contains(line.end());
        if (!startIn && !endIn) return nullptr;
        std::unique_ptr<Entity> clone = e.clone();
        if (startIn) clone->moveGripPoint(0, line.start() + delta);
        if (endIn) clone->moveGripPoint(1, line.end() + delta);
        return clone;
    }
    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(e);
        const bool startIn = window.contains(arc.startPoint());
        const bool endIn = window.contains(arc.endPoint());
        if (startIn && endIn && window.contains(arc.center())) {
            std::unique_ptr<Entity> clone = e.clone();
            clone->translate(delta);
            return clone;
        }
        if (!startIn && !endIn) return nullptr;
        // Endpoint moves keep center and radius: the point slides along the
        // arc toward the shifted position, like a grip edit.
        std::unique_ptr<Entity> clone = e.clone();
        if (startIn) clone->moveGripPoint(0, arc.startPoint() + delta);
        if (endIn) clone->moveGripPoint(1, arc.endPoint() + delta);
        return clone;
    }
    case EntityType::Circle:
        return translateIfInside(e, static_cast<const CircleEntity&>(e).center(), window, delta);
    case EntityType::Ellipse:
        return translateIfInside(e, static_cast<const EllipseEntity&>(e).center(), window, delta);
    case EntityType::Text:
        return translateIfInside(e, static_cast<const TextEntity&>(e).position(), window, delta);
    case EntityType::MText:
        return translateIfInside(e, static_cast<const MTextEntity&>(e).position(), window, delta);
    case EntityType::Insert:
        return translateIfInside(e, static_cast<const InsertEntity&>(e).position(), window, delta);
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Hatch:
    case EntityType::Leader:
    case EntityType::Dimension:
        return stretchByGrips(e, window, delta);
    }
    return nullptr;
}

std::optional<double> curveLength(const Entity& e) {
    switch (e.type()) {
    case EntityType::Line:
        return static_cast<const LineEntity&>(e).start().distanceTo(static_cast<const LineEntity&>(e).end());
    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(e);
        return arc.radius() * arcSweep(arc);
    }
    case EntityType::Polyline: {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        if (pl.closed()) return std::nullopt;
        double total = 0.0;
        pl.forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
            if (const auto arc = bulgeToArc(a, b, bulge)) {
                total += arc->radius * std::abs(arc->sweep);
            } else {
                total += a.distanceTo(b);
            }
        });
        return total;
    }
    default:
        return std::nullopt;
    }
}

std::unique_ptr<Entity> lengthenedClone(const Entity& e, const Point2D& pickPt, double deltaLen) {
    if (e.type() == EntityType::Line) {
        const auto& line = static_cast<const LineEntity&>(e);
        const Point2D d = line.end() - line.start();
        const double len = d.length();
        if (len < kEps) return nullptr;
        const Point2D dir = d * (1.0 / len);
        const bool atEnd = pickPt.distanceTo(line.end()) <= pickPt.distanceTo(line.start());
        if (len + deltaLen < kEps) return nullptr; // would collapse or invert
        std::unique_ptr<Entity> clone = e.clone();
        if (atEnd) {
            clone->moveGripPoint(1, line.end() + dir * deltaLen);
        } else {
            clone->moveGripPoint(0, line.start() - dir * deltaLen);
        }
        return clone;
    }
    if (e.type() == EntityType::Arc) {
        const auto& arc = static_cast<const ArcEntity&>(e);
        if (arc.radius() < kEps) return nullptr;
        const double sweep = arcSweep(arc);
        const double dAng = deltaLen / arc.radius();
        const double newSweep = sweep + dAng;
        if (newSweep < 1e-6 || newSweep > kTwoPi - 1e-6) return nullptr;
        const bool atEnd = pickPt.distanceTo(arc.endPoint()) <= pickPt.distanceTo(arc.startPoint());
        const double newAngle = atEnd ? arc.startAngle() + newSweep : arc.endAngle() - newSweep;
        const Point2D newPos(arc.center().x + arc.radius() * std::cos(newAngle),
                             arc.center().y + arc.radius() * std::sin(newAngle));
        std::unique_ptr<Entity> clone = e.clone();
        clone->moveGripPoint(atEnd ? 1 : 0, newPos);
        return clone;
    }
    return nullptr;
}

BreakResult breakEntity(const Entity& e, const Point2D& a, const Point2D& b,
                        const std::function<EntityId()>& makeId) {
    BreakResult result;

    switch (e.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(e);
        const Point2D d = line.end() - line.start();
        const double lenSq = d.dot(d);
        if (lenSq < kEps) return result;
        const double ta = std::clamp((a - line.start()).dot(d) / lenSq, 0.0, 1.0);
        const double tb = std::clamp((b - line.start()).dot(d) / lenSq, 0.0, 1.0);
        const double t1 = std::min(ta, tb);
        const double t2 = std::max(ta, tb);
        result.ok = true;
        constexpr double kTolT = 1e-6;
        if (t1 > kTolT) {
            auto piece = std::make_unique<LineEntity>(makeId(), e.layer(), line.start(), line.start() + d * t1);
            copyStyle(e, *piece);
            result.pieces.push_back(std::move(piece));
        }
        if (t2 < 1.0 - kTolT) {
            auto piece = std::make_unique<LineEntity>(makeId(), e.layer(), line.start() + d * t2, line.end());
            copyStyle(e, *piece);
            result.pieces.push_back(std::move(piece));
        }
        return result;
    }
    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(e);
        const double sweep = arcSweep(arc);
        const auto paramOf = [&](const Point2D& p) {
            const double ang = std::atan2(p.y - arc.center().y, p.x - arc.center().x);
            double s = normalizeAngle(ang - arc.startAngle());
            // Points that project past the arc's end clamp to the nearer end.
            if (s > sweep) s = (s - sweep < kTwoPi - s) ? sweep : 0.0;
            return s;
        };
        const double sa = paramOf(a);
        const double sb = paramOf(b);
        const double s1 = std::min(sa, sb);
        const double s2 = std::max(sa, sb);
        result.ok = true;
        constexpr double kTolS = 1e-6;
        if (s1 > kTolS) {
            auto piece = std::make_unique<ArcEntity>(makeId(), e.layer(), arc.center(), arc.radius(),
                                                     arc.startAngle(), arc.startAngle() + s1);
            copyStyle(e, *piece);
            result.pieces.push_back(std::move(piece));
        }
        if (s2 < sweep - kTolS) {
            auto piece = std::make_unique<ArcEntity>(makeId(), e.layer(), arc.center(), arc.radius(),
                                                     arc.startAngle() + s2, arc.startAngle() + sweep);
            copyStyle(e, *piece);
            result.pieces.push_back(std::move(piece));
        }
        return result;
    }
    case EntityType::Circle: {
        const auto& circle = static_cast<const CircleEntity&>(e);
        const double angA = std::atan2(a.y - circle.center().y, a.x - circle.center().x);
        const double angB = std::atan2(b.y - circle.center().y, b.x - circle.center().x);
        if (std::abs(normalizeAngle(angA - angB)) < 1e-6 ||
            std::abs(normalizeAngle(angA - angB) - kTwoPi) < 1e-6) {
            return result; // a circle can't be broken at a single point
        }
        // AutoCAD removes the CCW stretch from the first to the second point,
        // leaving the arc that runs CCW from the second back to the first.
        result.ok = true;
        auto piece = std::make_unique<ArcEntity>(makeId(), e.layer(), circle.center(), circle.radius(), angB, angA);
        copyStyle(e, *piece);
        result.pieces.push_back(std::move(piece));
        return result;
    }
    case EntityType::Polyline: {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        if (pl.closed() || pl.hasArcs() || pl.vertices().size() < 2) return result;
        const auto& verts = pl.vertices();
        // Locate a point along the vertex chain as (segment index, param).
        struct ChainPos {
            std::size_t seg = 0;
            double t = 0.0;
            Point2D point;
        };
        const auto locate = [&](const Point2D& p) {
            ChainPos best;
            double bestDist = std::numeric_limits<double>::max();
            for (std::size_t i = 0; i + 1 < verts.size(); ++i) {
                const Point2D d = verts[i + 1] - verts[i];
                const double lenSq = d.dot(d);
                const double t = lenSq < kEps ? 0.0 : std::clamp((p - verts[i]).dot(d) / lenSq, 0.0, 1.0);
                const Point2D onSeg = verts[i] + d * t;
                const double dist = onSeg.distanceTo(p);
                if (dist < bestDist) {
                    bestDist = dist;
                    best = {i, t, onSeg};
                }
            }
            return best;
        };
        ChainPos pa = locate(a);
        ChainPos pb = locate(b);
        if (pb.seg < pa.seg || (pb.seg == pa.seg && pb.t < pa.t)) std::swap(pa, pb);

        std::vector<Point2D> head(verts.begin(), verts.begin() + pa.seg + 1);
        if (head.empty() || head.back().distanceTo(pa.point) > kEps) head.push_back(pa.point);
        std::vector<Point2D> tail;
        if (pb.point.distanceTo(verts[pb.seg + 1]) > kEps) tail.push_back(pb.point);
        tail.insert(tail.end(), verts.begin() + pb.seg + 1, verts.end());

        result.ok = true;
        const auto emit = [&](std::vector<Point2D> chain) {
            if (chain.size() < 2) return;
            double len = 0.0;
            for (std::size_t i = 0; i + 1 < chain.size(); ++i) len += chain[i].distanceTo(chain[i + 1]);
            if (len < 1e-6) return;
            auto piece = std::make_unique<PolylineEntity>(makeId(), e.layer(), std::move(chain), false);
            copyStyle(e, *piece);
            result.pieces.push_back(std::move(piece));
        };
        emit(std::move(head));
        emit(std::move(tail));
        return result;
    }
    default:
        return result;
    }
}

} // namespace lcad
