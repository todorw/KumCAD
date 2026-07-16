#include "core/core3d/SketchToFace.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <vector>

namespace lcad {

namespace {

struct Loop {
    TopoDS_Wire wire;
    double signedArea = 0.0; // shoelace sign for line loops; a fixed +1-scaled magnitude for circle loops
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

// Greedily chains non-construction lines into closed point-index loops.
// Assumes each point has degree <= 2 in the profile (a simple polygon) --
// disclosed in the header.
std::vector<std::vector<int>> findClosedLineLoops(const Sketch& sketch) {
    std::vector<int> lineIndices;
    for (std::size_t i = 0; i < sketch.lines().size(); ++i) {
        if (!sketch.lines()[i].construction) lineIndices.push_back(static_cast<int>(i));
    }
    std::vector<bool> used(lineIndices.size(), false);
    std::vector<std::vector<int>> loops;

    for (std::size_t startIdx = 0; startIdx < lineIndices.size(); ++startIdx) {
        if (used[startIdx]) continue;
        used[startIdx] = true;
        const SketchLine& startLine = sketch.lines()[static_cast<std::size_t>(lineIndices[startIdx])];
        std::vector<int> loopPoints = {startLine.p1, startLine.p2};

        bool extended = true;
        while (extended) {
            extended = false;
            for (std::size_t i = 0; i < lineIndices.size(); ++i) {
                if (used[i]) continue;
                const SketchLine& line = sketch.lines()[static_cast<std::size_t>(lineIndices[i])];
                if (line.p1 == loopPoints.back() && line.p2 != loopPoints.back()) {
                    loopPoints.push_back(line.p2);
                    used[i] = true;
                    extended = true;
                } else if (line.p2 == loopPoints.back() && line.p1 != loopPoints.back()) {
                    loopPoints.push_back(line.p1);
                    used[i] = true;
                    extended = true;
                }
            }
        }

        if (loopPoints.size() >= 3 && loopPoints.back() == loopPoints.front()) {
            loopPoints.pop_back(); // last point duplicates the first once closed
            loops.push_back(std::move(loopPoints));
        }
        // An unclosed chain (open profile) is silently dropped -- it can't
        // bound a face, and there's no separate "open sketch" use case yet.
    }
    return loops;
}

} // namespace

std::optional<TopoDS_Face> sketchToFace(const Sketch& sketch) {
    std::vector<Loop> loops;

    for (const auto& loopPointIndices : findClosedLineLoops(sketch)) {
        std::vector<Point2D> pts;
        pts.reserve(loopPointIndices.size());
        for (int idx : loopPointIndices) pts.push_back(sketch.points()[static_cast<std::size_t>(idx)]);

        BRepBuilderAPI_MakeWire wireBuilder;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const Point2D& a = pts[i];
            const Point2D& b = pts[(i + 1) % pts.size()];
            wireBuilder.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(a.x, a.y, 0.0), gp_Pnt(b.x, b.y, 0.0)).Edge());
        }
        if (!wireBuilder.IsDone()) continue;
        loops.push_back({wireBuilder.Wire(), shoelaceSignedArea(pts)});
    }

    for (const auto& circle : sketch.circles()) {
        if (circle.construction) continue;
        const Point2D& center = sketch.points()[static_cast<std::size_t>(circle.center)];
        const gp_Ax2 axes(gp_Pnt(center.x, center.y, 0.0), gp_Dir(0, 0, 1));
        const gp_Circ gpCircle(axes, circle.radius);
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
