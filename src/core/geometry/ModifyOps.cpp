#include "core/geometry/ModifyOps.h"

#include "core/geometry/Arc.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/Circle.h"
#include "core/geometry/ConstructionLine.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Image.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Junction.h"
#include "core/geometry/Line.h"
#include "core/geometry/MText.h"
#include "core/geometry/NetLabel.h"
#include "core/geometry/NoConnect.h"
#include "core/geometry/PointCloud.h"
#include "core/geometry/PointEnt.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Text.h"
#include "core/geometry/Via.h"

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
    case EntityType::Point:
        return translateIfInside(e, static_cast<const PointEntity&>(e).position(), window, delta);
    case EntityType::ConstructionLine:
        return translateIfInside(e, static_cast<const ConstructionLineEntity&>(e).basePoint(), window, delta);
    case EntityType::AttDef:
        return translateIfInside(e, static_cast<const AttDefEntity&>(e).position(), window, delta);
    case EntityType::Image:
        return translateIfInside(e, static_cast<const ImageEntity&>(e).position(), window, delta);
    case EntityType::PointCloud:
        return translateIfInside(e, static_cast<const PointCloudEntity&>(e).boundingBox().min, window, delta);
    case EntityType::Junction:
        return translateIfInside(e, static_cast<const JunctionEntity&>(e).position(), window, delta);
    case EntityType::NoConnect:
        return translateIfInside(e, static_cast<const NoConnectEntity&>(e).position(), window, delta);
    case EntityType::NetLabel:
        return translateIfInside(e, static_cast<const NetLabelEntity&>(e).position(), window, delta);
    case EntityType::Via:
        return translateIfInside(e, static_cast<const ViaEntity&>(e).position(), window, delta);
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Hatch:
    case EntityType::Leader:
    case EntityType::MLeader:
    case EntityType::Dimension:
    case EntityType::Table:
    case EntityType::Wire:
    case EntityType::Track:
    case EntityType::Wipeout:
    case EntityType::Region:
    case EntityType::MLine:
    case EntityType::Tolerance:
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
    if (e.type() == EntityType::Polyline) {
        // Only the polyline's terminal segment (nearer pickPt) changes,
        // matching AutoCAD LENGTHEN on a polyline; the rest of the chain is
        // untouched. A bulged terminal segment keeps its center and radius,
        // like the Arc case, and gets its bulge recomputed for the new sweep.
        const auto& pl = static_cast<const PolylineEntity&>(e);
        if (pl.closed() || pl.vertices().size() < 2) return nullptr;
        const auto& verts = pl.vertices();
        const std::size_t n = verts.size();
        const bool atEnd = pickPt.distanceTo(verts.back()) <= pickPt.distanceTo(verts.front());
        const std::size_t segIdx = atEnd ? n - 2 : 0;
        const Point2D& segStart = verts[segIdx];
        const Point2D& segEnd = verts[segIdx + 1];
        const double bulge = pl.bulgeAt(segIdx);

        std::unique_ptr<Entity> clone = e.clone();
        auto& newPl = static_cast<PolylineEntity&>(*clone);

        if (const auto arc = bulgeToArc(segStart, segEnd, bulge)) {
            if (arc->radius < kEps) return nullptr;
            const double sign = arc->sweep >= 0 ? 1.0 : -1.0;
            const double newSweep = arc->sweep + sign * (deltaLen / arc->radius);
            if (std::abs(newSweep) < 1e-6 || std::abs(newSweep) > kTwoPi - 1e-6) return nullptr;
            if (atEnd) {
                const double newEndAngle = arc->startAngle + newSweep;
                newPl.moveGripPoint(segIdx + 1, arc->center + Point2D(std::cos(newEndAngle), std::sin(newEndAngle)) *
                                                                  arc->radius);
            } else {
                const double newStartAngle = (arc->startAngle + arc->sweep) - newSweep;
                newPl.moveGripPoint(segIdx, arc->center + Point2D(std::cos(newStartAngle), std::sin(newStartAngle)) *
                                                               arc->radius);
            }
            newPl.setBulge(segIdx, std::tan(newSweep / 4.0));
        } else {
            const Point2D d = segEnd - segStart;
            const double len = d.length();
            if (len < kEps || len + deltaLen < kEps) return nullptr;
            const Point2D dir = d * (1.0 / len);
            if (atEnd) newPl.moveGripPoint(segIdx + 1, segEnd + dir * deltaLen);
            else newPl.moveGripPoint(segIdx, segStart - dir * deltaLen);
        }
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
        if (pl.closed() || pl.vertices().size() < 2) return result;
        const auto& verts = pl.vertices();
        // Locate a point along the vertex chain as (segment index, param):
        // t is a plain 0..1 fraction on straight segments, and the
        // start-to-end sweep fraction on bulged ones, so the two compare the
        // same way regardless of segment kind.
        struct ChainPos {
            std::size_t seg = 0;
            double t = 0.0;
            Point2D point;
        };
        const auto locate = [&](const Point2D& p) {
            ChainPos best;
            double bestDist = std::numeric_limits<double>::max();
            for (std::size_t i = 0; i + 1 < verts.size(); ++i) {
                const double bulge = pl.bulgeAt(i);
                if (const auto arc = bulgeToArc(verts[i], verts[i + 1], bulge)) {
                    const double ang = std::atan2(p.y - arc->center.y, p.x - arc->center.x);
                    double rel = std::fmod(ang - arc->startAngle, kTwoPi);
                    if (arc->sweep >= 0) {
                        if (rel < 0) rel += kTwoPi;
                    } else if (rel > 0) {
                        rel -= kTwoPi;
                    }
                    const double t = std::clamp(rel / arc->sweep, 0.0, 1.0);
                    const double theta = arc->startAngle + arc->sweep * t;
                    const Point2D onSeg = arc->center + Point2D(std::cos(theta), std::sin(theta)) * arc->radius;
                    const double dist = onSeg.distanceTo(p);
                    if (dist < bestDist) {
                        bestDist = dist;
                        best = {i, t, onSeg};
                    }
                } else {
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
            }
            return best;
        };
        ChainPos pa = locate(a);
        ChainPos pb = locate(b);
        if (pb.seg < pa.seg || (pb.seg == pa.seg && pb.t < pa.t)) std::swap(pa, pb);

        constexpr double kTolT = 1e-6;
        // The bulge remaining on a sub-arc that covers [from, to] of the
        // original segment's sweep (bulge = tan(includedAngle / 4)).
        const auto subBulge = [&](std::size_t seg, double from, double to) {
            const auto arc = bulgeToArc(verts[seg], verts[seg + 1], pl.bulgeAt(seg));
            if (!arc) return 0.0;
            return std::tan(arc->sweep * (to - from) / 4.0);
        };

        std::vector<Point2D> headVerts(verts.begin(), verts.begin() + pa.seg + 1);
        std::vector<double> headBulges(pl.bulges().begin(), pl.bulges().begin() + pa.seg);
        if (pa.t > kTolT) {
            headBulges.push_back(subBulge(pa.seg, 0.0, pa.t));
            headVerts.push_back(pa.point);
        }

        std::vector<Point2D> tailVerts;
        std::vector<double> tailBulges;
        if (pb.t < 1.0 - kTolT) {
            tailVerts.push_back(pb.point);
            tailBulges.push_back(subBulge(pb.seg, pb.t, 1.0));
        }
        for (std::size_t i = pb.seg + 1; i < verts.size(); ++i) {
            tailVerts.push_back(verts[i]);
            if (i + 1 < verts.size()) tailBulges.push_back(pl.bulgeAt(i));
        }

        result.ok = true;
        const auto emit = [&](std::vector<Point2D> chainVerts, std::vector<double> chainBulges) {
            if (chainVerts.size() < 2) return;
            double len = 0.0;
            for (std::size_t i = 0; i + 1 < chainVerts.size(); ++i) len += chainVerts[i].distanceTo(chainVerts[i + 1]);
            if (len < 1e-6) return;
            auto piece = std::make_unique<PolylineEntity>(makeId(), e.layer(), std::move(chainVerts),
                                                           std::move(chainBulges), false);
            copyStyle(e, *piece);
            result.pieces.push_back(std::move(piece));
        };
        emit(std::move(headVerts), std::move(headBulges));
        emit(std::move(tailVerts), std::move(tailBulges));
        return result;
    }
    default:
        return result;
    }
}

std::optional<Point2D> pointAtDistance(const Entity& e, double s) {
    if (s < 0) return std::nullopt;
    switch (e.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(e);
        const Point2D d = line.end() - line.start();
        const double len = d.length();
        if (len < kEps || s > len + kEps) return std::nullopt;
        return line.start() + d * (s / len);
    }
    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(e);
        if (arc.radius() < kEps) return std::nullopt;
        const double sweep = arcSweep(arc);
        const double ang = arc.startAngle() + s / arc.radius();
        if (s > sweep * arc.radius() + kEps) return std::nullopt;
        return Point2D(arc.center().x + arc.radius() * std::cos(ang),
                       arc.center().y + arc.radius() * std::sin(ang));
    }
    case EntityType::Circle: {
        const auto& circle = static_cast<const CircleEntity&>(e);
        if (circle.radius() < kEps || s > kTwoPi * circle.radius() + kEps) return std::nullopt;
        const double ang = s / circle.radius();
        return Point2D(circle.center().x + circle.radius() * std::cos(ang),
                       circle.center().y + circle.radius() * std::sin(ang));
    }
    case EntityType::Polyline: {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        std::optional<Point2D> found;
        double walked = 0.0;
        pl.forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
            if (found) return;
            if (const auto arc = bulgeToArc(a, b, bulge)) {
                const double segLen = arc->radius * std::abs(arc->sweep);
                if (s <= walked + segLen + kEps) {
                    const double frac = segLen < kEps ? 0.0 : (s - walked) / segLen;
                    const double ang = arc->startAngle + arc->sweep * frac;
                    found = Point2D(arc->center.x + arc->radius * std::cos(ang),
                                    arc->center.y + arc->radius * std::sin(ang));
                    return;
                }
                walked += segLen;
            } else {
                const double segLen = a.distanceTo(b);
                if (s <= walked + segLen + kEps) {
                    const double frac = segLen < kEps ? 0.0 : (s - walked) / segLen;
                    found = a + (b - a) * frac;
                    return;
                }
                walked += segLen;
            }
        });
        return found;
    }
    default:
        return std::nullopt;
    }
}

namespace {

// Length basis for DIVIDE/MEASURE: circles and closed polylines measure
// their full perimeter (curveLength() deliberately refuses closed curves
// for LENGTHEN).
std::optional<double> sampleLength(const Entity& e) {
    if (e.type() == EntityType::Circle) {
        return kTwoPi * static_cast<const CircleEntity&>(e).radius();
    }
    if (e.type() == EntityType::Polyline) {
        const auto& pl = static_cast<const PolylineEntity&>(e);
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
    return curveLength(e);
}

} // namespace

std::vector<Point2D> divideEntity(const Entity& e, int n) {
    std::vector<Point2D> result;
    if (n < 2) return result;
    const auto length = sampleLength(e);
    if (!length || *length < kEps) return result;
    // Circles get n points (the "start" is also a division); open curves the
    // n-1 interior ones.
    const bool closed = e.type() == EntityType::Circle ||
                        (e.type() == EntityType::Polyline && static_cast<const PolylineEntity&>(e).closed());
    const int first = closed ? 0 : 1;
    for (int i = first; i < n; ++i) {
        if (const auto p = pointAtDistance(e, *length * i / n)) result.push_back(*p);
    }
    return result;
}

std::vector<Point2D> measureEntity(const Entity& e, double step) {
    std::vector<Point2D> result;
    if (step < kEps) return result;
    const auto length = sampleLength(e);
    if (!length) return result;
    for (double s = step; s <= *length + kEps; s += step) {
        if (const auto p = pointAtDistance(e, s)) result.push_back(*p);
    }
    return result;
}

namespace {

bool anglesClose(double a, double b, double tol) {
    const double diff = std::abs(normalizeAngle(a) - normalizeAngle(b));
    return std::min(diff, kTwoPi - diff) < tol;
}

bool sameGeometry(const Entity& a, const Entity& b, double tolerance) {
    if (a.type() != b.type() || a.layer() != b.layer()) return false;
    switch (a.type()) {
    case EntityType::Line: {
        const auto& la = static_cast<const LineEntity&>(a);
        const auto& lb = static_cast<const LineEntity&>(b);
        const bool sameOrder = la.start().distanceTo(lb.start()) < tolerance && la.end().distanceTo(lb.end()) < tolerance;
        const bool swapped = la.start().distanceTo(lb.end()) < tolerance && la.end().distanceTo(lb.start()) < tolerance;
        return sameOrder || swapped;
    }
    case EntityType::Circle: {
        const auto& ca = static_cast<const CircleEntity&>(a);
        const auto& cb = static_cast<const CircleEntity&>(b);
        return ca.center().distanceTo(cb.center()) < tolerance && std::abs(ca.radius() - cb.radius()) < tolerance;
    }
    case EntityType::Arc: {
        const auto& aa = static_cast<const ArcEntity&>(a);
        const auto& ab = static_cast<const ArcEntity&>(b);
        return aa.center().distanceTo(ab.center()) < tolerance && std::abs(aa.radius() - ab.radius()) < tolerance &&
               anglesClose(aa.startAngle(), ab.startAngle(), tolerance) &&
               anglesClose(aa.endAngle(), ab.endAngle(), tolerance);
    }
    case EntityType::Polyline: {
        const auto& pa = static_cast<const PolylineEntity&>(a);
        const auto& pb = static_cast<const PolylineEntity&>(b);
        if (pa.closed() != pb.closed() || pa.vertices().size() != pb.vertices().size()) return false;
        for (std::size_t i = 0; i < pa.vertices().size(); ++i) {
            if (pa.vertices()[i].distanceTo(pb.vertices()[i]) >= tolerance) return false;
            if (std::abs(pa.bulgeAt(i) - pb.bulgeAt(i)) >= tolerance) return false;
        }
        return true;
    }
    default:
        return false;
    }
}

} // namespace

std::vector<std::size_t> findDuplicateEntities(const std::vector<const Entity*>& entities, double tolerance) {
    std::vector<std::size_t> duplicates;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (!entities[i]) continue;
        for (std::size_t j = 0; j < i; ++j) {
            if (!entities[j]) continue;
            if (sameGeometry(*entities[i], *entities[j], tolerance)) {
                duplicates.push_back(i);
                break;
            }
        }
    }
    return duplicates;
}

} // namespace lcad
