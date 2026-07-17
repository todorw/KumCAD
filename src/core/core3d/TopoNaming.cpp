#include "core/core3d/TopoNaming.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>

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

} // namespace lcad
