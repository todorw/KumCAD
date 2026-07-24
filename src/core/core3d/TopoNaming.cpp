#include "core/core3d/TopoNaming.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <limits>

namespace lcad {

std::optional<EdgeFingerprint> fingerprintEdge(const TopoDS_Shape& shape, int index) {
    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
    if (index < 0 || index >= edgeMap.Extent()) return std::nullopt;

    const TopoDS_Edge edge = TopoDS::Edge(edgeMap(index + 1)); // map is 1-based, index is 0-based
    BRepAdaptor_Curve curve(edge);
    const gp_Pnt mid = curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2.0);

    GProp_GProps props;
    BRepGProp::LinearProperties(edge, props);

    return EdgeFingerprint{mid.X(), mid.Y(), mid.Z(), props.Mass()};
}

std::optional<FaceFingerprint> fingerprintFace(const TopoDS_Shape& shape, int index) {
    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
    if (index < 0 || index >= faceMap.Extent()) return std::nullopt;

    const TopoDS_Face face = TopoDS::Face(faceMap(index + 1));
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    const gp_Pnt centroid = props.CentreOfMass();

    return FaceFingerprint{centroid.X(), centroid.Y(), centroid.Z(), props.Mass()};
}

int resolveEdgeIndex(const TopoDS_Shape& shape, const EdgeFingerprint& target) {
    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);

    int best = -1;
    double bestScore = std::numeric_limits<double>::max();
    for (int i = 1; i <= edgeMap.Extent(); ++i) {
        const auto fp = fingerprintEdge(shape, i - 1);
        if (!fp) continue;
        const double dx = fp->midX - target.midX, dy = fp->midY - target.midY, dz = fp->midZ - target.midZ;
        const double midDistSq = dx * dx + dy * dy + dz * dz;
        const double lengthDiff = std::abs(fp->length - target.length);
        // Midpoint distance dominates (squared, so it grows fast); length
        // difference only breaks close ties -- weighted down so it can't
        // override a clearly-closer midpoint.
        const double score = midDistSq + lengthDiff * lengthDiff * 1e-6;
        if (score < bestScore) {
            bestScore = score;
            best = i - 1;
        }
    }
    return best;
}

int resolveFaceIndex(const TopoDS_Shape& shape, const FaceFingerprint& target) {
    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);

    int best = -1;
    double bestScore = std::numeric_limits<double>::max();
    for (int i = 1; i <= faceMap.Extent(); ++i) {
        const auto fp = fingerprintFace(shape, i - 1);
        if (!fp) continue;
        const double dx = fp->centroidX - target.centroidX, dy = fp->centroidY - target.centroidY,
                    dz = fp->centroidZ - target.centroidZ;
        const double centroidDistSq = dx * dx + dy * dy + dz * dz;
        const double areaDiff = std::abs(fp->area - target.area);
        const double score = centroidDistSq + areaDiff * areaDiff * 1e-6;
        if (score < bestScore) {
            bestScore = score;
            best = i - 1;
        }
    }
    return best;
}

std::optional<SketchPlane> planeFromFace(const TopoDS_Shape& shape, int index) {
    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
    if (index < 0 || index >= faceMap.Extent()) return std::nullopt;

    const TopoDS_Face face = TopoDS::Face(faceMap(index + 1));
    BRepAdaptor_Surface surf(face);
    if (surf.GetType() != GeomAbs_Plane) return std::nullopt;

    const gp_Pln pln = surf.Plane();
    const gp_Ax3 ax3 = pln.Position();
    const gp_Pnt origin = ax3.Location();
    gp_Dir normal = ax3.Direction();
    if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
    const gp_Dir xdir = ax3.XDirection();

    SketchPlane plane;
    plane.origin = {origin.X(), origin.Y(), origin.Z()};
    plane.normal = {normal.X(), normal.Y(), normal.Z()};
    plane.xAxis = {xdir.X(), xdir.Y(), xdir.Z()};
    return plane;
}

std::optional<EdgeAxis> axisFromEdge(const TopoDS_Shape& shape, int index) {
    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
    if (index < 0 || index >= edgeMap.Extent()) return std::nullopt;

    const TopoDS_Edge edge = TopoDS::Edge(edgeMap(index + 1));
    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() != GeomAbs_Line) return std::nullopt;

    const gp_Pnt p0 = curve.Value(curve.FirstParameter());
    const gp_Pnt p1 = curve.Value(curve.LastParameter());
    gp_Vec dir(p0, p1);
    if (dir.Magnitude() < 1e-12) return std::nullopt;
    dir.Normalize();

    EdgeAxis axis;
    axis.pointX = p0.X();
    axis.pointY = p0.Y();
    axis.pointZ = p0.Z();
    axis.dirX = dir.X();
    axis.dirY = dir.Y();
    axis.dirZ = dir.Z();
    return axis;
}

std::optional<VertexPoint> pointFromVertex(const TopoDS_Shape& shape, int index) {
    TopTools_IndexedMapOfShape vertexMap;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertexMap);
    if (index < 0 || index >= vertexMap.Extent()) return std::nullopt;

    const TopoDS_Vertex vertex = TopoDS::Vertex(vertexMap(index + 1));
    const gp_Pnt p = BRep_Tool::Pnt(vertex);

    VertexPoint result;
    result.x = p.X();
    result.y = p.Y();
    result.z = p.Z();
    return result;
}

std::optional<EdgeCircle> centerOfCircularEdge(const TopoDS_Shape& shape, int index) {
    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
    if (index < 0 || index >= edgeMap.Extent()) return std::nullopt;

    const TopoDS_Edge edge = TopoDS::Edge(edgeMap(index + 1));
    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() != GeomAbs_Circle) return std::nullopt;

    const gp_Circ circle = curve.Circle();
    const gp_Pnt center = circle.Location();
    const gp_Dir normal = circle.Axis().Direction();

    EdgeCircle result;
    result.centerX = center.X();
    result.centerY = center.Y();
    result.centerZ = center.Z();
    result.normalX = normal.X();
    result.normalY = normal.Y();
    result.normalZ = normal.Z();
    result.radius = circle.Radius();
    return result;
}

} // namespace lcad
