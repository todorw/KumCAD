#include "core/core3d/SketchToFace.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lcad {

namespace {

gp_Pnt toGpPnt(const SketchPlane& plane, const Point2D& local) {
    const Point3D w = plane.toWorld(local);
    return gp_Pnt(w.x, w.y, w.z);
}

// An axis system located at local's world position, whose main direction
// is the plane's own normal and whose X direction is the plane's own
// xAxis -- so a Geom_Circle/Geom_TrimmedCurve built on it parameterizes
// by the SAME angle already computed in local 2D coordinates (atan2 in
// makeArcEdge), regardless of how the plane itself is rotated in world
// space.
gp_Ax2 toGpAx2(const SketchPlane& plane, const Point2D& local) {
    const Point3D n = plane.normal;
    const Point3D x = plane.xAxis;
    return gp_Ax2(toGpPnt(plane, local), gp_Dir(n.x, n.y, n.z), gp_Dir(x.x, x.y, x.z));
}

struct Loop {
    TopoDS_Wire wire;
    double signedArea = 0.0; // shoelace sign for line/arc loops; a fixed +1-scaled magnitude for circle loops
};

double shoelaceSignedArea(const std::vector<Point2D>& pts) {
    double sum = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const Point2D& a = pts[i];
        const Point2D& b = pts[(i + 1) % pts.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return 0.5 * sum;
}

// One traversal step in a chained loop: a straight edge (arcIndex < 0) or
// an arc (arcIndex into sketch.arcs()) from p1 to p2 -- p1/p2 may be
// swapped relative to how the underlying SketchLine/SketchArc stores its
// own endpoints, tracked via `reversed` so arc sweep direction comes out
// right when the wire is actually built.
struct ChainEdge {
    int p1 = -1;
    int p2 = -1;
    int arcIndex = -1;
    bool reversed = false;
};

// Greedily chains non-construction lines AND arcs into closed point-index
// loops (mixed line/arc profiles, e.g. a rounded rectangle, chain exactly
// like an all-line profile since both are just "edges with two point
// endpoints" to this algorithm). Assumes each point has degree <= 2 in the
// profile (a simple polygon) -- disclosed in the header.
std::vector<std::vector<ChainEdge>> findClosedLoops(const Sketch& sketch) {
    std::vector<ChainEdge> edges;
    for (std::size_t i = 0; i < sketch.lines().size(); ++i) {
        if (sketch.lines()[i].construction) continue;
        edges.push_back({sketch.lines()[i].p1, sketch.lines()[i].p2, -1, false});
    }
    for (std::size_t i = 0; i < sketch.arcs().size(); ++i) {
        if (sketch.arcs()[i].construction) continue;
        edges.push_back({sketch.arcs()[i].start, sketch.arcs()[i].end, static_cast<int>(i), false});
    }

    std::vector<bool> used(edges.size(), false);
    std::vector<std::vector<ChainEdge>> loops;

    for (std::size_t startIdx = 0; startIdx < edges.size(); ++startIdx) {
        if (used[startIdx]) continue;
        used[startIdx] = true;
        std::vector<ChainEdge> chain = {edges[startIdx]};
        const int head = edges[startIdx].p1;
        int tail = edges[startIdx].p2;

        bool extended = true;
        while (extended) {
            extended = false;
            for (std::size_t i = 0; i < edges.size(); ++i) {
                if (used[i]) continue;
                ChainEdge e = edges[i];
                if (e.p1 == tail && e.p2 != tail) {
                    chain.push_back(e);
                    tail = e.p2;
                    used[i] = true;
                    extended = true;
                } else if (e.p2 == tail && e.p1 != tail) {
                    std::swap(e.p1, e.p2);
                    e.reversed = true;
                    chain.push_back(e);
                    tail = e.p2;
                    used[i] = true;
                    extended = true;
                }
            }
        }

        if (chain.size() >= 2 && tail == head) loops.push_back(std::move(chain));
        // An unclosed chain (open profile) is silently dropped -- it can't
        // bound a face, and there's no separate "open sketch" use case yet.
    }
    return loops;
}

TopoDS_Edge makeArcEdge(const Sketch& sketch, const SketchArc& arc, const ChainEdge& edge) {
    const Point2D& center = sketch.points()[static_cast<std::size_t>(arc.center)];
    const Point2D& p1 = sketch.points()[static_cast<std::size_t>(edge.p1)];
    const Point2D& p2 = sketch.points()[static_cast<std::size_t>(edge.p2)];

    const bool effectiveCcw = edge.reversed ? !arc.ccw : arc.ccw;
    double startAngle = std::atan2(p1.y - center.y, p1.x - center.x);
    double endAngle = std::atan2(p2.y - center.y, p2.x - center.x);
    if (effectiveCcw) {
        while (endAngle < startAngle) endAngle += 2.0 * M_PI;
    } else {
        while (endAngle > startAngle) endAngle -= 2.0 * M_PI;
    }
    const double u1 = std::min(startAngle, endAngle);
    const double u2 = std::max(startAngle, endAngle);

    // XDirection pinned to the sketch plane's own local +X so the circle's
    // parameter is a plain polar angle in LOCAL coordinates, matching
    // startAngle/endAngle computed via atan2 above regardless of how the
    // plane itself is oriented in world space -- the same fix
    // SheetMetal.cpp's arc edges needed, generalized past the flat-Z=0
    // assumption.
    const gp_Ax2 axis = toGpAx2(sketch.placement(), center);
    Handle(Geom_Circle) circle = new Geom_Circle(axis, arc.radius);
    Handle(Geom_TrimmedCurve) trimmed = new Geom_TrimmedCurve(circle, u1, u2);
    return BRepBuilderAPI_MakeEdge(trimmed).Edge();
}

} // namespace

std::optional<TopoDS_Face> sketchToFace(const Sketch& sketch) {
    std::vector<Loop> loops;

    for (const auto& chain : findClosedLoops(sketch)) {
        std::vector<Point2D> pts;
        pts.reserve(chain.size());
        for (const ChainEdge& edge : chain) pts.push_back(sketch.points()[static_cast<std::size_t>(edge.p1)]);

        BRepBuilderAPI_MakeWire wireBuilder;
        for (const ChainEdge& edge : chain) {
            if (edge.arcIndex < 0) {
                const Point2D& a = sketch.points()[static_cast<std::size_t>(edge.p1)];
                const Point2D& b = sketch.points()[static_cast<std::size_t>(edge.p2)];
                wireBuilder.Add(
                    BRepBuilderAPI_MakeEdge(toGpPnt(sketch.placement(), a), toGpPnt(sketch.placement(), b)).Edge());
            } else {
                wireBuilder.Add(makeArcEdge(sketch, sketch.arcs()[static_cast<std::size_t>(edge.arcIndex)], edge));
            }
        }
        if (!wireBuilder.IsDone()) continue;
        // Chord-polygon area (arcs approximated by their chords here) --
        // fine for the "which loop is the outer boundary" comparison
        // below; the wire actually added to the face above uses the real
        // arc geometry, not this approximation.
        loops.push_back({wireBuilder.Wire(), shoelaceSignedArea(pts)});
    }

    for (const auto& circle : sketch.circles()) {
        if (circle.construction) continue;
        const Point2D& center = sketch.points()[static_cast<std::size_t>(circle.center)];
        const gp_Circ gpCircle(toGpAx2(sketch.placement(), center), circle.radius);
        BRepBuilderAPI_MakeWire wireBuilder(BRepBuilderAPI_MakeEdge(gpCircle).Edge());
        if (!wireBuilder.IsDone()) continue;
        loops.push_back({wireBuilder.Wire(), 3.14159265358979323846 * circle.radius * circle.radius});
    }

    if (loops.empty()) return std::nullopt;

    const auto outerIt = std::max_element(loops.begin(), loops.end(), [](const Loop& a, const Loop& b) {
        return std::abs(a.signedArea) < std::abs(b.signedArea);
    });
    const double outerSignedArea = outerIt->signedArea;

    BRepBuilderAPI_MakeFace faceBuilder(outerIt->wire, Standard_False);
    for (auto it = loops.begin(); it != loops.end(); ++it) {
        if (it == outerIt) continue;
        TopoDS_Wire holeWire = it->wire;
        // A hole must wind opposite to the outer boundary; reverse it if it
        // doesn't already.
        const bool sameSign = (it->signedArea >= 0) == (outerSignedArea >= 0);
        if (sameSign) holeWire = TopoDS::Wire(holeWire.Reversed());
        faceBuilder.Add(holeWire);
    }

    if (!faceBuilder.IsDone()) return std::nullopt;
    return faceBuilder.Face();
}

} // namespace lcad
