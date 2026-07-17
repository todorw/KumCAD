#include "core/core3d/Cam3D.h"

#include "core/geometry/Polyline.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAbs_CurveType.hxx>
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

constexpr int kCurveSamples = 32;

// Every edge's own two endpoints, plus (for a curved edge) the
// tessellated points between them -- so a pocket wall milled from a
// cylindrical face still comes out as a real polygon approximation of
// the circle, not just its two endpoints collapsed into a chord.
struct SampledEdge {
    Point2D a, b; // == samples.front()/samples.back()
    std::vector<Point2D> samples;
};

SampledEdge sampleEdge(const TopoDS_Edge& edge, const gp_Pnt& p1, const gp_Pnt& p2) {
    SampledEdge result;
    result.a = Point2D(p1.X(), p1.Y());
    result.b = Point2D(p2.X(), p2.Y());

    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() == GeomAbs_Line) {
        result.samples = {result.a, result.b};
        return result;
    }
    const double u1 = curve.FirstParameter();
    const double u2 = curve.LastParameter();
    for (int s = 0; s <= kCurveSamples; ++s) {
        const double u = u1 + (u2 - u1) * (static_cast<double>(s) / kCurveSamples);
        const gp_Pnt p = curve.Value(u);
        result.samples.emplace_back(p.X(), p.Y());
    }
    return result;
}

double loopArea(const std::vector<Point2D>& pts) {
    double sum = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const Point2D& a = pts[i];
        const Point2D& b = pts[(i + 1) % pts.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return std::abs(sum) * 0.5;
}

// Slices shape at z and chains the section's edges into closed loops,
// returning every one of them sorted largest-area-first (index 0 is
// always the outer boundary -- see Cam3DLevel's own comment on how the
// rest, islands/pockets, get contoured too now instead of being dropped).
//
// A plane cutting straight through a cylindrical pocket wall produces a
// SINGLE edge whose two "endpoints" are the same point (a closed/seamed
// circular edge, not two vertices with a gap) -- a real, easy-to-miss
// case (caught by a real test expecting a pocket's own contour to show
// up, not by inspection): such an edge IS already a complete loop by
// itself and is handled before the general shared-endpoint chaining
// below, which only applies to edges that actually connect to a
// DIFFERENT edge at each end.
std::vector<std::vector<Point2D>> allLoopsAt(const TopoDS_Shape& shape, double z) {
    BRepAlgoAPI_Section section(shape, gp_Pln(gp_Pnt(0.0, 0.0, z), gp_Dir(0.0, 0.0, 1.0)), Standard_True);
    if (!section.IsDone()) return {};

    constexpr double kTol = 1e-6;
    auto near = [](const Point2D& a, const Point2D& b) { return a.distanceTo(b) < kTol; };

    std::vector<std::vector<Point2D>> loops;
    std::vector<SampledEdge> edges;
    for (TopExp_Explorer exp(section.Shape(), TopAbs_EDGE); exp.More(); exp.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(exp.Current());
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(edge, v1, v2);
        if (v1.IsNull() || v2.IsNull()) continue;

        SampledEdge sampled = sampleEdge(edge, BRep_Tool::Pnt(v1), BRep_Tool::Pnt(v2));
        if (near(sampled.a, sampled.b)) {
            // Self-closing (a full circle/seamed edge) -- already a
            // complete loop on its own.
            if (sampled.samples.size() >= 4) {
                sampled.samples.pop_back(); // last duplicates first once closed
                loops.push_back(std::move(sampled.samples));
            }
            continue;
        }
        edges.push_back(std::move(sampled));
    }

    std::vector<bool> used(edges.size(), false);
    for (std::size_t start = 0; start < edges.size(); ++start) {
        if (used[start]) continue;
        used[start] = true;
        std::vector<Point2D> chain = edges[start].samples;

        bool extended = true;
        while (extended) {
            extended = false;
            for (std::size_t i = 0; i < edges.size(); ++i) {
                if (used[i]) continue;
                if (near(edges[i].a, chain.back())) {
                    chain.insert(chain.end(), edges[i].samples.begin() + 1, edges[i].samples.end());
                    used[i] = true;
                    extended = true;
                } else if (near(edges[i].b, chain.back())) {
                    for (auto it = edges[i].samples.rbegin() + 1; it != edges[i].samples.rend(); ++it) chain.push_back(*it);
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

    std::sort(loops.begin(), loops.end(), [](const std::vector<Point2D>& a, const std::vector<Point2D>& b) {
        return loopArea(a) > loopArea(b);
    });
    return loops;
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
        const std::vector<std::vector<Point2D>> loops = allLoopsAt(shape, z);
        if (loops.empty() || loops[0].size() < 3) continue; // no material at this level -- not an error, just skip it

        Cam3DLevel level;
        level.z = z;

        const PolylineEntity outerProfile(0, 0, loops[0], true);
        const std::vector<Point2D> outerPath = computeToolpath(outerProfile, toolParams);
        if (outerPath.size() >= 2) level.toolpaths.push_back(outerPath);

        for (std::size_t i = 1; i < loops.size(); ++i) {
            if (loops[i].size() < 3) continue;
            const PolylineEntity islandProfile(0, 0, loops[i], true);
            ToolpathParams islandParams = toolParams;
            islandParams.side = CutSide::Outside; // stay clear of the island, regardless of params.side
            const std::vector<Point2D> islandPath = computeToolpath(islandProfile, islandParams);
            if (islandPath.size() >= 2) level.toolpaths.push_back(islandPath);
        }

        if (!level.toolpaths.empty()) levels.push_back(std::move(level));
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
        for (const std::vector<Point2D>& toolpath : level.toolpaths) {
            if (toolpath.size() < 2) continue;
            out << "G0 Z" << params.safeHeight << "\n";
            out << "G0 X" << toolpath[0].x << " Y" << toolpath[0].y << "\n";
            out << "G1 Z" << level.z << " F" << params.plungeRate << "\n";
            for (std::size_t i = 1; i < toolpath.size(); ++i) {
                out << "G1 X" << toolpath[i].x << " Y" << toolpath[i].y << " F" << params.feedRate << "\n";
            }
        }
    }
    out << "G0 Z" << params.safeHeight << "\n";
    out << "M30\n";
    return static_cast<bool>(out);
}

} // namespace lcad
