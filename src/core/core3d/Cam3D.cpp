#include "core/core3d/Cam3D.h"

#include "core/geometry/Polyline.h"

#include <BRepAlgoAPI_Section.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace lcad {

namespace {

struct Edge2D {
    Point2D a, b;
};

double loopArea(const std::vector<Point2D>& pts) {
    double sum = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const Point2D& a = pts[i];
        const Point2D& b = pts[(i + 1) % pts.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return std::abs(sum) * 0.5;
}

// Slices shape at z and greedily chains the section's edges (by shared
// endpoint, within tolerance) into closed loops, keeping only the largest
// -- see Cam3D.h's own disclosure on why this drops islands/pockets.
std::vector<Point2D> largestLoopAt(const TopoDS_Shape& shape, double z) {
    BRepAlgoAPI_Section section(shape, gp_Pln(gp_Pnt(0.0, 0.0, z), gp_Dir(0.0, 0.0, 1.0)), Standard_True);
    if (!section.IsDone()) return {};

    std::vector<Edge2D> edges;
    for (TopExp_Explorer exp(section.Shape(), TopAbs_EDGE); exp.More(); exp.Next()) {
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(TopoDS::Edge(exp.Current()), v1, v2);
        if (v1.IsNull() || v2.IsNull()) continue;
        const gp_Pnt p1 = BRep_Tool::Pnt(v1);
        const gp_Pnt p2 = BRep_Tool::Pnt(v2);
        edges.push_back({Point2D(p1.X(), p1.Y()), Point2D(p2.X(), p2.Y())});
    }
    if (edges.empty()) return {};

    constexpr double kTol = 1e-6;
    auto near = [](const Point2D& a, const Point2D& b) { return a.distanceTo(b) < kTol; };

    std::vector<bool> used(edges.size(), false);
    std::vector<std::vector<Point2D>> loops;
    for (std::size_t start = 0; start < edges.size(); ++start) {
        if (used[start]) continue;
        used[start] = true;
        std::vector<Point2D> chain = {edges[start].a, edges[start].b};

        bool extended = true;
        while (extended) {
            extended = false;
            for (std::size_t i = 0; i < edges.size(); ++i) {
                if (used[i]) continue;
                if (near(edges[i].a, chain.back())) {
                    chain.push_back(edges[i].b);
                    used[i] = true;
                    extended = true;
                } else if (near(edges[i].b, chain.back())) {
                    chain.push_back(edges[i].a);
                    used[i] = true;
                    extended = true;
                }
            }
        }

        if (chain.size() >= 4 && near(chain.front(), chain.back())) {
            chain.pop_back(); // last point duplicates the first once closed
            loops.push_back(std::move(chain));
        }
    }
    if (loops.empty()) return {};

    return *std::max_element(loops.begin(), loops.end(),
                              [](const std::vector<Point2D>& a, const std::vector<Point2D>& b) {
                                  return loopArea(a) < loopArea(b);
                              });
}

} // namespace

std::vector<Cam3DLevel> sliceIntoLevels(const TopoDS_Shape& shape, const Cam3DParams& params) {
    std::vector<Cam3DLevel> levels;
    if (shape.IsNull() || params.stepDown <= 1e-9) return levels;

    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double totalHeight = zmax - zmin;
    if (totalHeight <= 1e-9) return levels;

    // Slicing exactly at the top or bottom face tends to produce a
    // degenerate (tangent) section, so the first pass starts one stepDown
    // below the top and the last pass sits a small epsilon above the true
    // bottom rather than exactly on it.
    const double epsilon = std::min(params.stepDown, totalHeight) * 0.05;
    std::vector<double> zLevels;
    for (double z = zmax - params.stepDown; z > zmin + epsilon; z -= params.stepDown) zLevels.push_back(z);
    zLevels.push_back(zmin + epsilon);

    ToolpathParams toolParams;
    toolParams.toolDiameter = params.toolDiameter;
    toolParams.side = params.side;

    for (double z : zLevels) {
        const std::vector<Point2D> loop = largestLoopAt(shape, z);
        if (loop.size() < 3) continue; // no material at this level -- not an error, just skip it

        const PolylineEntity profile(0, 0, loop, true);
        const std::vector<Point2D> path = computeToolpath(profile, toolParams);
        if (path.size() < 2) continue;
        levels.push_back({z, path});
    }
    return levels;
}

bool writeMultiLevelGCode(const std::vector<Cam3DLevel>& levels, const Cam3DParams& params, const std::string& path,
                          std::string* errorOut) {
    if (levels.empty()) {
        if (errorOut) *errorOut = "No levels to machine";
        return false;
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (errorOut) *errorOut = "Could not open output file";
        return false;
    }

    out << "G21\nG90\n";
    for (const Cam3DLevel& level : levels) {
        if (level.toolpath.size() < 2) continue;
        out << "G0 Z" << params.safeHeight << "\n";
        out << "G0 X" << level.toolpath[0].x << " Y" << level.toolpath[0].y << "\n";
        out << "G1 Z" << level.z << " F" << params.plungeRate << "\n";
        for (std::size_t i = 1; i < level.toolpath.size(); ++i) {
            out << "G1 X" << level.toolpath[i].x << " Y" << level.toolpath[i].y << " F" << params.feedRate << "\n";
        }
    }
    out << "G0 Z" << params.safeHeight << "\n";
    out << "M30\n";
    return static_cast<bool>(out);
}

} // namespace lcad
