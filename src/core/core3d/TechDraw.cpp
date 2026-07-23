#include "core/core3d/TechDraw.h"

#include "core/geometry/Dimension.h"
#include "core/geometry/Line.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAbs_CurveType.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

namespace lcad {

namespace {

// The eye (main) direction and reference X direction for each standard
// view -- fixed, documented conventions rather than something derived, so
// every view is reproducible without needing to visually eyeball it (see
// TechDraw.h's own disclosure about this environment having no display).
gp_Ax2 axisFor(ViewDirection direction) {
    switch (direction) {
    case ViewDirection::Front:
        return gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0)); // looking along -Y
    case ViewDirection::Top:
        return gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, -1), gp_Dir(1, 0, 0)); // looking along -Z
    case ViewDirection::Right:
        return gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(-1, 0, 0), gp_Dir(0, 1, 0)); // looking along -X
    case ViewDirection::Iso:
        return gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(-1, -1, -1), gp_Dir(1, -1, 0)); // classic isometric eye
    }
    return gp_Ax2();
}

// HLRBRep_Algo's VCompound()/HCompound() shapes are already expressed in
// the projector's own local 2D frame -- every vertex comes back with Z
// (numerically) 0, and X/Y are already the view-plane coordinates. An
// earlier draft of this function re-projected these points through the
// world-space axis a second time, which happened to silently zero out or
// scramble extents (caught by this sprint's own dimension-check tests,
// not by inspection) -- so this deliberately does *no* further transform.
//
// A curved edge (a cylinder's silhouette, a filleted corner, ...) is
// tessellated into kCurveSegments straight chords via BRepAdaptor_Curve
// sampling rather than emitted as one long chord end-to-end -- real
// drafting-quality curve projection (exact conics/splines in the output)
// is deeper territory than this codebase's own vector-line 2D engine
// supports anyway (LineEntity only, no arc-in-DXF-sense concept at the
// TechDraw level), so a fine tessellation is the honest, useful middle
// ground: visually smooth at reasonable scales, still just LineEntity
// segments once baked into the 2D document.
constexpr int kCurveSegments = 32;

void collectEdges(const TopoDS_Shape& compound, bool hidden, std::vector<ProjectedEdge>& out) {
    if (compound.IsNull()) return;
    for (TopExp_Explorer exp(compound, TopAbs_EDGE); exp.More(); exp.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(exp.Current());

        BRepAdaptor_Curve curve(edge);
        if (curve.GetType() == GeomAbs_Line) {
            TopoDS_Vertex v1, v2;
            TopExp::Vertices(edge, v1, v2);
            if (v1.IsNull() || v2.IsNull()) continue;
            const gp_Pnt p1 = BRep_Tool::Pnt(v1);
            const gp_Pnt p2 = BRep_Tool::Pnt(v2);
            ProjectedEdge pe;
            pe.hidden = hidden;
            pe.x1 = p1.X();
            pe.y1 = p1.Y();
            pe.x2 = p2.X();
            pe.y2 = p2.Y();
            out.push_back(pe);
            continue;
        }

        const double u1 = curve.FirstParameter();
        const double u2 = curve.LastParameter();
        gp_Pnt previous = curve.Value(u1);
        for (int s = 1; s <= kCurveSegments; ++s) {
            const double u = u1 + (u2 - u1) * (static_cast<double>(s) / kCurveSegments);
            const gp_Pnt next = curve.Value(u);
            ProjectedEdge pe;
            pe.hidden = hidden;
            pe.x1 = previous.X();
            pe.y1 = previous.Y();
            pe.x2 = next.X();
            pe.y2 = next.Y();
            out.push_back(pe);
            previous = next;
        }
    }
}

TechDrawView projectFromAxis(const TopoDS_Shape& shape, const gp_Ax2& axis) {
    TechDrawView view;
    if (shape.IsNull()) return view;

    Handle(HLRBRep_Algo) hlr = new HLRBRep_Algo();
    hlr->Add(shape);
    hlr->Projector(HLRAlgo_Projector(axis));
    hlr->Update();
    hlr->Hide();

    HLRBRep_HLRToShape extractor(hlr);
    // Sharp (non-tangent) visible/hidden edges cover the vast majority of
    // real parts (boxes, extrusions, machined pockets); smooth-edge and
    // outline-only cases (a sphere's silhouette) are a disclosed gap --
    // real drafting-quality curve projection is deeper territory than this
    // sprint's "prove 3D-to-2D projection works end to end" scope.
    collectEdges(extractor.VCompound(), false, view.edges);
    collectEdges(extractor.HCompound(), true, view.edges);
    return view;
}

// A generously-sized axis-aligned-to-the-cut-plane box covering the whole
// positive-normal half-space near origin: corner offset -half along both
// in-plane axes (so it's centered on origin across the plane), extending
// 2*half laterally and half along +normal -- exactly the "material to
// remove" half-space BRepAlgoAPI_Cut needs, without relying on
// BRepPrimAPI_MakeHalfSpace's own reference-point convention.
TopoDS_Shape buildCuttingHalfSpaceBox(const gp_Pnt& origin, const gp_Dir& normal, double half) {
    const gp_Dir arbitrary = (std::abs(normal.Z()) < 0.9) ? gp_Dir(0, 0, 1) : gp_Dir(1, 0, 0);
    const gp_Vec xVec = gp_Vec(arbitrary) - gp_Vec(normal) * gp_Vec(arbitrary).Dot(gp_Vec(normal));
    const gp_Dir xDir(xVec);
    const gp_Ax2 originAxis(origin, normal, xDir); // Z=normal, X=xDir, Y=normal^xDir (right-handed, auto)
    const gp_Pnt corner = origin.Translated(gp_Vec(xDir) * (-half)).Translated(gp_Vec(originAxis.YDirection()) * (-half));
    const gp_Ax2 boxAxis(corner, normal, xDir);
    return BRepPrimAPI_MakeBox(boxAxis, 2.0 * half, 2.0 * half, half).Shape();
}

} // namespace

TechDrawView projectView(const TopoDS_Shape& shape, ViewDirection direction) {
    if (shape.IsNull()) return TechDrawView{};
    return projectFromAxis(shape, axisFor(direction));
}

TechDrawView projectViewAux(const TopoDS_Shape& shape, double eyeX, double eyeY, double eyeZ, double upX, double upY,
                            double upZ) {
    if (shape.IsNull()) return TechDrawView{};
    const double eyeMag = std::sqrt(eyeX * eyeX + eyeY * eyeY + eyeZ * eyeZ);
    if (eyeMag < 1e-9) return TechDrawView{};
    const gp_Dir eye(eyeX, eyeY, eyeZ);

    // Project up perpendicular to eye (camera "up vector" convention) so
    // up need not be exactly perpendicular itself -- only not parallel.
    const gp_Vec upVec(upX, upY, upZ);
    if (upVec.Magnitude() < 1e-9) return TechDrawView{};
    const gp_Vec eyeVec(eye);
    const gp_Vec xVec = upVec - eyeVec * upVec.Dot(eyeVec);
    if (xVec.Magnitude() < 1e-9) return TechDrawView{}; // up was parallel to eye -- no well-defined view

    const gp_Ax2 axis(gp_Pnt(0, 0, 0), eye, gp_Dir(xVec));
    return projectFromAxis(shape, axis);
}

TechDrawView projectSectionView(const TopoDS_Shape& shape, ViewDirection direction, double originX, double originY,
                                double originZ, double normalX, double normalY, double normalZ) {
    if (shape.IsNull()) return TechDrawView{};
    const double normalMag = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
    if (normalMag < 1e-9) return TechDrawView{};

    Bnd_Box bbox;
    BRepBndLib::Add(shape, bbox);
    if (bbox.IsVoid()) return TechDrawView{};
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double half = std::max({xmax - xmin, ymax - ymin, zmax - zmin}) * 4.0 + 1.0;

    const gp_Pnt origin(originX, originY, originZ);
    const gp_Dir normal(normalX / normalMag, normalY / normalMag, normalZ / normalMag);
    const TopoDS_Shape halfSpace = buildCuttingHalfSpaceBox(origin, normal, half);

    BRepAlgoAPI_Cut cutOp(shape, halfSpace);
    if (!cutOp.IsDone()) return TechDrawView{};

    return projectFromAxis(cutOp.Shape(), axisFor(direction));
}

void insertViewIntoDocument(Document& doc2d, const TechDrawView& view, double offsetX, double offsetY) {
    LayerId techDrawLayer = 0;
    bool found = false;
    for (const Layer& layer : doc2d.layers()) {
        if (layer.name == "TECHDRAW") {
            techDrawLayer = layer.id;
            found = true;
            break;
        }
    }
    if (!found) techDrawLayer = doc2d.addLayer("TECHDRAW", Color{0, 200, 255});

    for (const ProjectedEdge& edge : view.edges) {
        const EntityId id = doc2d.reserveEntityId();
        auto line = std::make_unique<LineEntity>(id, techDrawLayer, Point2D(edge.x1 + offsetX, edge.y1 + offsetY),
                                                  Point2D(edge.x2 + offsetX, edge.y2 + offsetY));
        if (edge.hidden) line->setLinetypeOverride(LineType::Hidden);
        doc2d.addEntity(std::move(line));
    }
}

void autoDimensionView(Document& doc2d, const TechDrawView& view, const AutoDimensionOptions& options) {
    if (view.edges.empty()) return;

    constexpr double kEps = 1e-6;

    double minX = std::numeric_limits<double>::max(), minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest(), maxY = std::numeric_limits<double>::lowest();
    for (const ProjectedEdge& e : view.edges) {
        minX = std::min({minX, e.x1, e.x2});
        minY = std::min({minY, e.y1, e.y2});
        maxX = std::max({maxX, e.x1, e.x2});
        maxY = std::max({maxY, e.y1, e.y2});
    }
    if (maxX < minX || maxY < minY) return;

    LayerId dimLayer = 0;
    bool found = false;
    for (const Layer& layer : doc2d.layers()) {
        if (layer.name == "DIMENSIONS") {
            dimLayer = layer.id;
            found = true;
            break;
        }
    }
    if (!found) dimLayer = doc2d.addLayer("DIMENSIONS", Color{255, 128, 0});

    const double ox = options.offsetX, oy = options.offsetY;
    const double gap = options.dimensionGap;
    auto addLinear = [&](Point2D p1, Point2D p2, Point2D linePoint) {
        doc2d.addEntity(std::make_unique<DimensionEntity>(doc2d.reserveEntityId(), dimLayer, p1, p2, linePoint, false));
    };

    // Overall width, below the view; overall height, left of the view --
    // both exact since the projected coordinates already are the model's
    // real dimensions for an orthographic view (see this function's own
    // comment in TechDraw.h about why Iso isn't supported here).
    addLinear(Point2D(minX + ox, minY + oy), Point2D(maxX + ox, minY + oy),
             Point2D((minX + maxX) / 2.0 + ox, minY + oy - gap));
    addLinear(Point2D(minX + ox, minY + oy), Point2D(minX + ox, maxY + oy),
             Point2D(minX + ox - gap, (minY + maxY) / 2.0 + oy));

    if (!options.dimensionEachAxisAlignedEdge) return;

    auto quantize = [](double v) { return static_cast<long long>(std::llround(v / kEps)); };
    std::set<std::tuple<int, long long, long long, long long>> seen;
    for (const ProjectedEdge& e : view.edges) {
        if (e.hidden) continue;
        const double dx = e.x2 - e.x1, dy = e.y2 - e.y1;
        if (std::abs(dy) < kEps && std::abs(dx) > kEps) {
            const double y = e.y1;
            const double xa = std::min(e.x1, e.x2), xb = std::max(e.x1, e.x2);
            const auto key = std::make_tuple(0, quantize(y), quantize(xa), quantize(xb));
            if (!seen.insert(key).second) continue;
            addLinear(Point2D(xa + ox, y + oy), Point2D(xb + ox, y + oy), Point2D((xa + xb) / 2.0 + ox, y + oy - gap));
        } else if (std::abs(dx) < kEps && std::abs(dy) > kEps) {
            const double x = e.x1;
            const double ya = std::min(e.y1, e.y2), yb = std::max(e.y1, e.y2);
            const auto key = std::make_tuple(1, quantize(x), quantize(ya), quantize(yb));
            if (!seen.insert(key).second) continue;
            addLinear(Point2D(x + ox, ya + oy), Point2D(x + ox, yb + oy), Point2D(x + ox - gap, (ya + yb) / 2.0 + oy));
        }
    }
}

} // namespace lcad
