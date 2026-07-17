#include "core/core3d/Pick3D.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <GeomLProp_SLProps.hxx>
#include <IntCurvesFace_Intersector.hxx>
#include <Standard_Real.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace lcad {

std::optional<FacePickResult> pickFace(const TopoDS_Shape& shape, const PickRay& ray) {
    if (shape.IsNull()) return std::nullopt;
    const double dirLen = std::sqrt(ray.direction[0] * ray.direction[0] + ray.direction[1] * ray.direction[1] +
                                    ray.direction[2] * ray.direction[2]);
    if (dirLen < 1e-12) return std::nullopt;

    const gp_Pnt origin(ray.origin[0], ray.origin[1], ray.origin[2]);
    const gp_Dir direction(ray.direction[0], ray.direction[1], ray.direction[2]);
    const gp_Lin line(origin, direction);

    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);

    std::optional<FacePickResult> best;
    for (int i = 1; i <= faceMap.Extent(); ++i) {
        const TopoDS_Face face = TopoDS::Face(faceMap(i));
        // aRestr defaults true: intersections are already clipped to the
        // face's real trimmed boundary, not just its underlying infinite
        // surface, so every point IntCurvesFace_Intersector returns here
        // is genuinely ON the face.
        IntCurvesFace_Intersector intersector(face, 1e-6);
        intersector.Perform(line, 0.0, Standard_Real(RealLast()));
        if (!intersector.IsDone()) continue;

        for (int j = 1; j <= intersector.NbPnt(); ++j) {
            const double w = intersector.WParameter(j);
            if (w < 0.0) continue;
            if (best && w >= best->distance) continue;

            const gp_Pnt p = intersector.Pnt(j);
            GeomLProp_SLProps props(BRep_Tool::Surface(face), intersector.UParameter(j), intersector.VParameter(j), 1,
                                    1e-6);
            gp_Dir normal(0, 0, 1);
            if (props.IsNormalDefined()) normal = props.Normal();
            if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();

            FacePickResult result;
            result.faceIndex = i - 1;
            result.point = {p.X(), p.Y(), p.Z()};
            result.normal = {normal.X(), normal.Y(), normal.Z()};
            result.distance = w;
            best = result;
        }
    }
    return best;
}

std::optional<EdgePickResult> pickEdge(const TopoDS_Shape& shape, const PickRay& ray, double tolerance) {
    if (shape.IsNull()) return std::nullopt;
    const double dirLen = std::sqrt(ray.direction[0] * ray.direction[0] + ray.direction[1] * ray.direction[1] +
                                    ray.direction[2] * ray.direction[2]);
    if (dirLen < 1e-12) return std::nullopt;

    const gp_Pnt origin(ray.origin[0], ray.origin[1], ray.origin[2]);
    const gp_Dir direction(ray.direction[0], ray.direction[1], ray.direction[2]);

    // Perpendicular distance from p to origin/direction's own ray (not
    // the infinite line -- t < 0 means the foot-of-perpendicular falls
    // behind the ray's own origin, which pickFace already rejects via its
    // own w < 0 check, so this must match it or a "ray" pick could return
    // geometry that's actually behind the origin). footTOut is left
    // unset for a rejected (behind-origin) sample.
    auto distanceToRay = [&](const gp_Pnt& p, gp_Pnt& footOut) -> std::optional<double> {
        const gp_Vec toPoint(origin, p);
        const double t = toPoint.Dot(gp_Vec(direction));
        if (t < 0.0) return std::nullopt;
        const gp_Pnt foot = origin.Translated(gp_Vec(direction) * t);
        footOut = foot;
        return p.Distance(foot);
    };

    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);

    constexpr int kSamples = 24;
    std::optional<EdgePickResult> best;
    for (int i = 1; i <= edgeMap.Extent(); ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(edgeMap(i));
        BRepAdaptor_Curve curve(edge);
        const double u1 = curve.FirstParameter();
        const double u2 = curve.LastParameter();

        for (int s = 0; s <= kSamples; ++s) {
            const double u = u1 + (u2 - u1) * (static_cast<double>(s) / kSamples);
            const gp_Pnt p = curve.Value(u);
            gp_Pnt foot;
            const std::optional<double> distance = distanceToRay(p, foot);
            if (!distance || *distance > tolerance) continue;
            if (best && *distance >= best->distance) continue;

            EdgePickResult result;
            result.edgeIndex = i - 1;
            result.point = {p.X(), p.Y(), p.Z()};
            result.distance = *distance;
            best = result;
        }
    }
    return best;
}

} // namespace lcad
