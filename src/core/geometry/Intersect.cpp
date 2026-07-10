#include "core/geometry/Intersect.h"

#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Entity.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"

#include <cmath>

namespace lcad {

namespace {

constexpr double kEps = 1e-9;

// Shared segment/line vs segment solver. clampA=false treats a1..a2 as an
// infinite line; the b side is always a finite segment.
void lineSegment(const Point2D& a1, const Point2D& a2, const Point2D& b1, const Point2D& b2, bool clampA,
                 std::vector<Point2D>& out) {
    const Point2D r = a2 - a1;
    const Point2D s = b2 - b1;
    const double denom = r.x * s.y - r.y * s.x;
    if (std::abs(denom) < 1e-12) return; // parallel (or collinear): no unique point
    const Point2D qp = b1 - a1;
    const double t = (qp.x * s.y - qp.y * s.x) / denom;
    const double u = (qp.x * r.y - qp.y * r.x) / denom;
    if (u < -kEps || u > 1 + kEps) return;
    if (clampA && (t < -kEps || t > 1 + kEps)) return;
    out.push_back(a1 + r * t);
}

// Shared segment/line vs circle solver. clamp=false treats a1..a2 as infinite.
void lineCircle(const Point2D& a1, const Point2D& a2, const Point2D& center, double radius, bool clamp,
                std::vector<Point2D>& out) {
    const Point2D d = a2 - a1;
    const Point2D f = a1 - center;
    const double A = d.dot(d);
    if (A < 1e-12) return;
    const double B = 2.0 * f.dot(d);
    const double C = f.dot(f) - radius * radius;
    const double disc = B * B - 4.0 * A * C;
    if (disc < 0) return;
    const double root = std::sqrt(disc);
    const double t1 = (-B - root) / (2.0 * A);
    const double t2 = (-B + root) / (2.0 * A);
    auto accept = [&](double t) {
        if (clamp && (t < -kEps || t > 1 + kEps)) return;
        out.push_back(a1 + d * t);
    };
    accept(t1);
    if (root > kEps) accept(t2); // tangency: report the point once
}

// The finite curves an entity is made of, as segments plus circles (an arc --
// standalone or a bulged polyline segment -- is a circle whose points must
// additionally fall within a CCW angular sweep).
struct Primitives {
    std::vector<std::pair<Point2D, Point2D>> segments;
    struct Circ {
        Point2D center;
        double radius;
        bool restricted = false; // true: only points within the CCW sweep count
        double startAngle = 0.0;
        double endAngle = 0.0;
    };
    std::vector<Circ> circles;
};

Primitives primitivesOf(const Entity& e) {
    Primitives prims;
    switch (e.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(e);
        prims.segments.emplace_back(line.start(), line.end());
        break;
    }
    case EntityType::Polyline: {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        pl.forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
            if (const auto arc = bulgeToArc(a, b, bulge)) {
                const double lo = arc->sweep >= 0 ? arc->startAngle : arc->startAngle + arc->sweep;
                prims.circles.push_back({arc->center, arc->radius, true, lo, lo + std::abs(arc->sweep)});
            } else {
                prims.segments.emplace_back(a, b);
            }
        });
        break;
    }
    case EntityType::Circle: {
        const auto& c = static_cast<const CircleEntity&>(e);
        prims.circles.push_back({c.center(), c.radius()});
        break;
    }
    case EntityType::Arc: {
        const auto& a = static_cast<const ArcEntity&>(e);
        prims.circles.push_back({a.center(), a.radius(), true, a.startAngle(), a.endAngle()});
        break;
    }
    case EntityType::Spline: {
        // Approximated by its sampled polyline -- accurate enough for TRIM,
        // EXTEND, and OSNAP-intersection use.
        const auto& spline = static_cast<const SplineEntity&>(e);
        const auto pts = spline.sample(128);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) prims.segments.emplace_back(pts[i], pts[i + 1]);
        break;
    }
    default:
        break; // Ellipse/Text/Dimension: not intersectable (yet)
    }
    return prims;
}

bool onArc(const Primitives::Circ& c, const Point2D& p) {
    if (!c.restricted) return true;
    const double angle = std::atan2(p.y - c.center.y, p.x - c.center.x);
    double sweep = c.endAngle - c.startAngle;
    sweep = std::fmod(sweep, 2 * M_PI);
    if (sweep <= 0) sweep += 2 * M_PI;
    double rel = std::fmod(angle - c.startAngle, 2 * M_PI);
    if (rel < 0) rel += 2 * M_PI;
    return rel <= sweep + 1e-9;
}

void appendFiltered(const std::vector<Point2D>& pts, const Primitives::Circ* ca, const Primitives::Circ* cb,
                    std::vector<Point2D>& out) {
    for (const Point2D& p : pts) {
        if (ca && !onArc(*ca, p)) continue;
        if (cb && !onArc(*cb, p)) continue;
        out.push_back(p);
    }
}

} // namespace

std::vector<Point2D> intersectSegmentSegment(const Point2D& a1, const Point2D& a2, const Point2D& b1,
                                             const Point2D& b2) {
    std::vector<Point2D> out;
    lineSegment(a1, a2, b1, b2, true, out);
    return out;
}

std::vector<Point2D> intersectSegmentCircle(const Point2D& a1, const Point2D& a2, const Point2D& center,
                                            double radius) {
    std::vector<Point2D> out;
    lineCircle(a1, a2, center, radius, true, out);
    return out;
}

std::vector<Point2D> intersectCircleCircle(const Point2D& c1, double r1, const Point2D& c2, double r2) {
    std::vector<Point2D> out;
    const double d = c1.distanceTo(c2);
    if (d < kEps) return out;                     // concentric: none or infinite
    if (d > r1 + r2 + kEps) return out;           // too far apart
    if (d < std::abs(r1 - r2) - kEps) return out; // one inside the other
    const double a = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    const double hSq = r1 * r1 - a * a;
    const double h = hSq > 0 ? std::sqrt(hSq) : 0.0;
    const Point2D dir = (c2 - c1) * (1.0 / d);
    const Point2D mid = c1 + dir * a;
    const Point2D perp(-dir.y, dir.x);
    out.push_back(mid + perp * h);
    if (h > kEps) out.push_back(mid - perp * h);
    return out;
}

std::vector<Point2D> intersectEntities(const Entity& a, const Entity& b) {
    const Primitives pa = primitivesOf(a);
    const Primitives pb = primitivesOf(b);
    std::vector<Point2D> out;

    for (const auto& sa : pa.segments) {
        for (const auto& sb : pb.segments) {
            appendFiltered(intersectSegmentSegment(sa.first, sa.second, sb.first, sb.second), nullptr, nullptr, out);
        }
        for (const auto& cb : pb.circles) {
            appendFiltered(intersectSegmentCircle(sa.first, sa.second, cb.center, cb.radius), nullptr, &cb, out);
        }
    }
    for (const auto& ca : pa.circles) {
        for (const auto& sb : pb.segments) {
            appendFiltered(intersectSegmentCircle(sb.first, sb.second, ca.center, ca.radius), &ca, nullptr, out);
        }
        for (const auto& cb : pb.circles) {
            appendFiltered(intersectCircleCircle(ca.center, ca.radius, cb.center, cb.radius), &ca, &cb, out);
        }
    }
    return out;
}

std::vector<Point2D> intersectInfiniteLineEntity(const Point2D& a, const Point2D& b, const Entity& entity) {
    const Primitives prims = primitivesOf(entity);
    std::vector<Point2D> out;
    for (const auto& seg : prims.segments) lineSegment(a, b, seg.first, seg.second, false, out);
    for (const auto& c : prims.circles) {
        std::vector<Point2D> pts;
        lineCircle(a, b, c.center, c.radius, false, pts);
        appendFiltered(pts, &c, nullptr, out);
    }
    return out;
}

} // namespace lcad
