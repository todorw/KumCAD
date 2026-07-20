#include "core/pcb/BoardOutline.h"

#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

#include <algorithm>
#include <cmath>

namespace lcad {

bool pointInPolygon(const Point2D& p, const std::vector<Point2D>& poly) {
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Point2D& a = poly[i];
        const Point2D& b = poly[j];
        const bool crosses = ((a.y > p.y) != (b.y > p.y));
        if (!crosses) continue;
        const double xCross = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
        if (p.x < xCross) inside = !inside;
    }
    return inside;
}

namespace {

double polygonArea(const std::vector<Point2D>& pts) {
    if (pts.size() < 3) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const Point2D& a = pts[i];
        const Point2D& b = pts[(i + 1) % pts.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return std::abs(sum) / 2.0;
}

std::vector<Point2D> tessellateArc(const ArcEntity& arc, int segments = 32) {
    std::vector<Point2D> pts;
    pts.reserve(static_cast<std::size_t>(segments) + 1);
    double a0 = arc.startAngle();
    double a1 = arc.endAngle();
    while (a1 < a0) a1 += 2.0 * M_PI; // always sweeps CCW start->end, see ArcEntity's own comment
    for (int s = 0; s <= segments; ++s) {
        const double t = a0 + (a1 - a0) * static_cast<double>(s) / segments;
        pts.push_back(Point2D(arc.center().x + arc.radius() * std::cos(t), arc.center().y + arc.radius() * std::sin(t)));
    }
    return pts;
}

// One chainable, already-tessellated piece of Edge.Cuts geometry: a
// straight LineEntity's own 2 points, or an ArcEntity's tessellation --
// points.front()/points.back() are what chaining below matches against,
// the interior points (if any) just get spliced into the final loop
// as-is once the chain order is known.
struct RawEdge {
    std::vector<Point2D> points;
};

// Greedily chains edges by endpoint proximity (within tolerance) into
// closed loops -- position-based version of SketchToFace.cpp's own
// point-index-based findClosedLoops, needed here because Document
// entities don't share point indices the way Sketch geometry does.
// Assumes each endpoint touches at most 2 edges (a simple, non-branching
// board outline), the same simple-profile assumption SketchToFace.cpp's
// own chaining discloses.
std::vector<std::vector<Point2D>> chainIntoLoops(std::vector<RawEdge> edges, double tolerance) {
    std::vector<std::vector<Point2D>> loops;
    std::vector<bool> used(edges.size(), false);

    for (std::size_t startIdx = 0; startIdx < edges.size(); ++startIdx) {
        if (used[startIdx]) continue;
        used[startIdx] = true;
        std::vector<Point2D> chain = edges[startIdx].points;
        const Point2D head = chain.front();
        Point2D tail = chain.back();

        bool extended = true;
        while (extended) {
            extended = false;
            for (std::size_t i = 0; i < edges.size(); ++i) {
                if (used[i]) continue;
                const RawEdge& e = edges[i];
                if (e.points.front().distanceTo(tail) <= tolerance) {
                    chain.insert(chain.end(), e.points.begin() + 1, e.points.end());
                    tail = chain.back();
                    used[i] = true;
                    extended = true;
                } else if (e.points.back().distanceTo(tail) <= tolerance) {
                    chain.insert(chain.end(), e.points.rbegin() + 1, e.points.rend());
                    tail = chain.back();
                    used[i] = true;
                    extended = true;
                }
            }
        }

        if (chain.size() >= 3 && tail.distanceTo(head) <= tolerance) loops.push_back(std::move(chain));
        // An unclosed chain is silently dropped -- it can't bound a
        // board, same "not every open profile is a use case" call
        // SketchToFace.cpp's own findClosedLoops makes.
    }
    return loops;
}

} // namespace

std::vector<Point2D> deriveBoardOutline(const Document& doc, const std::string& layerName, double chainTolerance) {
    LayerId targetLayer = 0;
    bool found = false;
    for (const Layer& layer : doc.layers()) {
        if (layer.name == layerName) {
            targetLayer = layer.id;
            found = true;
            break;
        }
    }
    if (!found) return {};

    std::vector<std::vector<Point2D>> loops;
    std::vector<RawEdge> edges;

    for (const Entity* entity : doc.entities()) {
        if (entity->layer() != targetLayer) continue;
        switch (entity->type()) {
        case EntityType::Line: {
            const auto* line = static_cast<const LineEntity*>(entity);
            edges.push_back({{line->start(), line->end()}});
            break;
        }
        case EntityType::Arc: {
            edges.push_back({tessellateArc(static_cast<const ArcEntity&>(*entity))});
            break;
        }
        case EntityType::Polyline: {
            const auto* pl = static_cast<const PolylineEntity*>(entity);
            if (pl->vertices().size() < 2) break;
            if (pl->closed()) {
                // A closed polyline is a complete loop by itself --
                // bulges tessellated the same way PolylineOps elsewhere
                // in this codebase already resolves them (bulgeToArc).
                std::vector<Point2D> loopPts;
                for (std::size_t i = 0; i < pl->vertices().size(); ++i) {
                    loopPts.push_back(pl->vertices()[i]);
                    const std::size_t next = (i + 1) % pl->vertices().size();
                    const auto arc = bulgeToArc(pl->vertices()[i], pl->vertices()[next], pl->bulgeAt(i));
                    if (!arc) continue;
                    double a0 = arc->startAngle;
                    double a1 = a0 + arc->sweep;
                    for (int s = 1; s < 16; ++s) {
                        const double t = a0 + (a1 - a0) * s / 16.0;
                        loopPts.push_back(Point2D(arc->center.x + arc->radius * std::cos(t), arc->center.y + arc->radius * std::sin(t)));
                    }
                }
                loops.push_back(std::move(loopPts));
            } else {
                // An open polyline contributes to the general chain the
                // same way a standalone line/arc does -- its own
                // segments, bulges tessellated the same way.
                for (std::size_t i = 0; i + 1 < pl->vertices().size(); ++i) {
                    const auto arc = bulgeToArc(pl->vertices()[i], pl->vertices()[i + 1], pl->bulgeAt(i));
                    if (!arc) {
                        edges.push_back({{pl->vertices()[i], pl->vertices()[i + 1]}});
                        continue;
                    }
                    std::vector<Point2D> seg = {pl->vertices()[i]};
                    const double a0 = arc->startAngle, a1 = a0 + arc->sweep;
                    for (int s = 1; s < 16; ++s) {
                        const double t = a0 + (a1 - a0) * s / 16.0;
                        seg.push_back(Point2D(arc->center.x + arc->radius * std::cos(t), arc->center.y + arc->radius * std::sin(t)));
                    }
                    seg.push_back(pl->vertices()[i + 1]);
                    edges.push_back({std::move(seg)});
                }
            }
            break;
        }
        default:
            break; // not board-outline geometry
        }
    }

    for (auto& loop : chainIntoLoops(std::move(edges), chainTolerance)) loops.push_back(std::move(loop));

    if (loops.empty()) return {};
    return *std::max_element(loops.begin(), loops.end(), [](const std::vector<Point2D>& a, const std::vector<Point2D>& b) {
        return polygonArea(a) < polygonArea(b);
    });
}

bool pointInKeepout(const Point2D& p, LayerId layer, const std::vector<KeepoutZone>& zones, bool forPour) {
    for (const KeepoutZone& zone : zones) {
        if (forPour && !zone.blocksCopperPour) continue;
        if (!forPour && !zone.blocksAutorouting) continue;
        if (zone.layer.has_value() && *zone.layer != layer) continue;
        if (pointInPolygon(p, zone.polygon)) return true;
    }
    return false;
}

} // namespace lcad
