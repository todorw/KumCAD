#include "core/geometry/SnapGeometry.h"

#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Entity.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

#include <cmath>
#include <limits>

namespace lcad {

namespace {

constexpr double kTwoPi = 2.0 * M_PI;

double normalizeAngle(double angle) {
    angle = std::fmod(angle, kTwoPi);
    if (angle < 0) angle += kTwoPi;
    return angle;
}

Point2D nearestOnSegment(const Point2D& a, const Point2D& b, const Point2D& pt) {
    const Point2D d = b - a;
    const double lenSq = d.dot(d);
    if (lenSq < 1e-12) return a;
    const double t = std::clamp((pt - a).dot(d) / lenSq, 0.0, 1.0);
    return a + d * t;
}

Point2D nearestOnCircle(const Point2D& center, double radius, const Point2D& pt) {
    const Point2D d = pt - center;
    const double len = d.length();
    if (len < 1e-12) return center + Point2D(radius, 0.0);
    return center + d * (radius / len);
}

Point2D nearestOnArc(const ArcEntity& arc, const Point2D& pt) {
    const double ang = std::atan2(pt.y - arc.center().y, pt.x - arc.center().x);
    if (arc.containsAngle(ang)) return nearestOnCircle(arc.center(), arc.radius(), pt);
    const Point2D s = arc.startPoint();
    const Point2D e = arc.endPoint();
    return pt.distanceTo(s) <= pt.distanceTo(e) ? s : e;
}

} // namespace

std::optional<Point2D> nearestPointOnEntity(const Entity& e, const Point2D& pt) {
    switch (e.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(e);
        return nearestOnSegment(line.start(), line.end(), pt);
    }
    case EntityType::Circle: {
        const auto& circle = static_cast<const CircleEntity&>(e);
        return nearestOnCircle(circle.center(), circle.radius(), pt);
    }
    case EntityType::Arc:
        return nearestOnArc(static_cast<const ArcEntity&>(e), pt);
    case EntityType::Polyline: {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        Point2D best;
        double bestDist = std::numeric_limits<double>::max();
        pl.forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
            Point2D candidate;
            if (const auto arc = bulgeToArc(a, b, bulge)) {
                const double ang = std::atan2(pt.y - arc->center.y, pt.x - arc->center.x);
                // Accept the radial foot only within the bulge's sweep.
                const double rel = normalizeAngle(ang - arc->startAngle);
                const double sweepAbs = std::abs(arc->sweep);
                const bool inside = arc->sweep >= 0 ? rel <= sweepAbs : kTwoPi - rel <= sweepAbs || rel < 1e-9;
                if (inside) {
                    candidate = nearestOnCircle(arc->center, arc->radius, pt);
                } else {
                    candidate = pt.distanceTo(a) <= pt.distanceTo(b) ? a : b;
                }
            } else {
                candidate = nearestOnSegment(a, b, pt);
            }
            const double dist = candidate.distanceTo(pt);
            if (dist < bestDist) {
                bestDist = dist;
                best = candidate;
            }
        });
        if (bestDist == std::numeric_limits<double>::max()) return std::nullopt;
        return best;
    }
    case EntityType::Ellipse: {
        // Sampled: dense parametric scan plus one local refinement pass. Not
        // exact, but well under a pixel at drafting zoom levels.
        const auto& ellipse = static_cast<const EllipseEntity&>(e);
        const auto at = [&](double t) {
            const Point2D local(ellipse.radiusX() * std::cos(t), ellipse.radiusY() * std::sin(t));
            return ellipse.center() + rotateAround(local, Point2D(), ellipse.rotation());
        };
        double bestT = 0.0;
        double bestDist = std::numeric_limits<double>::max();
        constexpr int kSamples = 96;
        for (int i = 0; i < kSamples; ++i) {
            const double t = kTwoPi * i / kSamples;
            const double d = at(t).distanceTo(pt);
            if (d < bestDist) {
                bestDist = d;
                bestT = t;
            }
        }
        double step = kTwoPi / kSamples;
        for (int iter = 0; iter < 24; ++iter) {
            step *= 0.5;
            for (double t : {bestT - step, bestT + step}) {
                const double d = at(t).distanceTo(pt);
                if (d < bestDist) {
                    bestDist = d;
                    bestT = t;
                }
            }
        }
        return at(bestT);
    }
    default:
        return std::nullopt;
    }
}

std::vector<Point2D> perpendicularPoints(const Entity& e, const Point2D& from) {
    std::vector<Point2D> result;
    switch (e.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(e);
        const Point2D d = line.end() - line.start();
        const double lenSq = d.dot(d);
        if (lenSq < 1e-12) break;
        const double t = (from - line.start()).dot(d) / lenSq;
        if (t >= 0.0 && t <= 1.0) result.push_back(line.start() + d * t);
        break;
    }
    case EntityType::Circle: {
        const auto& circle = static_cast<const CircleEntity&>(e);
        const Point2D d = from - circle.center();
        const double len = d.length();
        if (len < 1e-12) break;
        const Point2D dir = d * (1.0 / len);
        result.push_back(circle.center() + dir * circle.radius());
        result.push_back(circle.center() - dir * circle.radius());
        break;
    }
    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(e);
        const Point2D d = from - arc.center();
        const double len = d.length();
        if (len < 1e-12) break;
        const Point2D dir = d * (1.0 / len);
        for (const Point2D& p : {arc.center() + dir * arc.radius(), arc.center() - dir * arc.radius()}) {
            const double ang = std::atan2(p.y - arc.center().y, p.x - arc.center().x);
            if (arc.containsAngle(ang)) result.push_back(p);
        }
        break;
    }
    case EntityType::Polyline: {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        pl.forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
            if (bulgeToArc(a, b, bulge)) return; // straight segments only
            const Point2D d = b - a;
            const double lenSq = d.dot(d);
            if (lenSq < 1e-12) return;
            const double t = (from - a).dot(d) / lenSq;
            if (t >= 0.0 && t <= 1.0) result.push_back(a + d * t);
        });
        break;
    }
    default:
        break;
    }
    return result;
}

std::vector<Point2D> tangentPoints(const Entity& e, const Point2D& from) {
    Point2D center;
    double radius = 0.0;
    const ArcEntity* arc = nullptr;
    if (e.type() == EntityType::Circle) {
        const auto& circle = static_cast<const CircleEntity&>(e);
        center = circle.center();
        radius = circle.radius();
    } else if (e.type() == EntityType::Arc) {
        arc = &static_cast<const ArcEntity&>(e);
        center = arc->center();
        radius = arc->radius();
    } else {
        return {};
    }

    const Point2D d = from - center;
    const double dist = d.length();
    if (dist <= radius + 1e-12 || radius < 1e-12) return {}; // inside or on the circle: no tangent

    const double base = std::atan2(d.y, d.x);
    const double offset = std::acos(radius / dist);
    std::vector<Point2D> result;
    for (double ang : {base + offset, base - offset}) {
        if (arc && !arc->containsAngle(ang)) continue;
        result.emplace_back(center.x + radius * std::cos(ang), center.y + radius * std::sin(ang));
    }
    return result;
}

} // namespace lcad
