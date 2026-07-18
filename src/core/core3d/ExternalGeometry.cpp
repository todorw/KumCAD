#include "core/core3d/ExternalGeometry.h"

#include <BRepAdaptor_Curve.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <cmath>

namespace lcad {

namespace {
Point2D projectToPlane(const gp_Pnt& p, const SketchPlane& plane) {
    const double dx = p.X() - plane.origin.x;
    const double dy = p.Y() - plane.origin.y;
    const double dz = p.Z() - plane.origin.z;
    const Point3D y = plane.yAxis();
    const double localX = dx * plane.xAxis.x + dy * plane.xAxis.y + dz * plane.xAxis.z;
    const double localY = dx * y.x + dy * y.y + dz * y.z;
    return Point2D(localX, localY);
}

void tessellateIntoSketch(Sketch& sketch, BRepAdaptor_Curve& curve, const SketchPlane& plane, int segments) {
    const double u1 = curve.FirstParameter();
    const double u2 = curve.LastParameter();
    int prevPoint = -1;
    for (int s = 0; s <= segments; ++s) {
        const double u = u1 + (u2 - u1) * (static_cast<double>(s) / segments);
        const Point2D local = projectToPlane(curve.Value(u), plane);
        const int pointIdx = sketch.addPoint(local, /*fixed=*/true);
        if (prevPoint >= 0) sketch.addLine(prevPoint, pointIdx, /*construction=*/true);
        prevPoint = pointIdx;
    }
}
} // namespace

bool projectExternalEdge(Sketch& sketch, const TopoDS_Shape& shape, int edgeIndex, int tessellationSegments) {
    if (shape.IsNull() || edgeIndex < 0 || tessellationSegments < 1) return false;

    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
    if (edgeIndex >= edgeMap.Extent()) return false;

    const TopoDS_Edge edge = TopoDS::Edge(edgeMap(edgeIndex + 1));
    BRepAdaptor_Curve curve(edge);
    const SketchPlane& plane = sketch.placement();

    if (curve.GetType() == GeomAbs_Line) {
        const Point2D a = projectToPlane(curve.Value(curve.FirstParameter()), plane);
        const Point2D b = projectToPlane(curve.Value(curve.LastParameter()), plane);
        const int pa = sketch.addPoint(a, /*fixed=*/true);
        const int pb = sketch.addPoint(b, /*fixed=*/true);
        sketch.addLine(pa, pb, /*construction=*/true);
        return true;
    }

    if (curve.GetType() == GeomAbs_Circle) {
        const gp_Circ circ = curve.Circle();
        const bool isFullCircle = std::abs((curve.LastParameter() - curve.FirstParameter()) - 2.0 * M_PI) < 1e-6;
        const gp_Dir axis = circ.Axis().Direction();
        const double planeNormalLen =
            std::sqrt(plane.normal.x * plane.normal.x + plane.normal.y * plane.normal.y + plane.normal.z * plane.normal.z);
        const double dot = planeNormalLen > 1e-12
                              ? (axis.X() * plane.normal.x + axis.Y() * plane.normal.y + axis.Z() * plane.normal.z) / planeNormalLen
                              : 0.0;
        const bool isParallel = std::abs(std::abs(dot) - 1.0) < 1e-6;

        if (isFullCircle && isParallel) {
            const Point2D center = projectToPlane(circ.Location(), plane);
            const int centerIdx = sketch.addPoint(center, /*fixed=*/true);
            sketch.addCircle(centerIdx, circ.Radius(), /*construction=*/true);
            return true;
        }
    }

    tessellateIntoSketch(sketch, curve, plane, tessellationSegments);
    return true;
}

} // namespace lcad
