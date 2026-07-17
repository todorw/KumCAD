#include "core/core3d/TechDraw.h"

#include "core/geometry/Line.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
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

} // namespace

TechDrawView projectView(const TopoDS_Shape& shape, ViewDirection direction) {
    TechDrawView view;
    if (shape.IsNull()) return view;

    const gp_Ax2 axis = axisFor(direction);

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

} // namespace lcad
