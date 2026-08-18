#include "core/core3d/ExternalGeometry.h"

#include "core/core3d/TopoNaming.h"

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

// atan2 angle, normalized to [0, 2*PI).
double positiveAngle(double angle) {
    while (angle < 0.0) angle += 2.0 * M_PI;
    while (angle >= 2.0 * M_PI) angle -= 2.0 * M_PI;
    return angle;
}

void tessellateIntoSketch(Sketch& sketch, BRepAdaptor_Curve& curve, const SketchPlane& plane, int segments,
                          ExternalGeometryRef* outRef) {
    const double u1 = curve.FirstParameter();
    const double u2 = curve.LastParameter();
    int prevPoint = -1;
    for (int s = 0; s <= segments; ++s) {
        const double u = u1 + (u2 - u1) * (static_cast<double>(s) / segments);
        const Point2D local = projectToPlane(curve.Value(u), plane);
        const int pointIdx = sketch.addPoint(local, /*fixed=*/true);
        if (outRef) outRef->pointIndices.push_back(pointIdx);
        if (prevPoint >= 0) {
            const int lineIdx = sketch.addLine(prevPoint, pointIdx, /*construction=*/true);
            if (outRef) outRef->lineIndices.push_back(lineIdx);
        }
        prevPoint = pointIdx;
    }
}

// Whether curve's own axis is parallel to plane's normal -- the
// no-foreshortening test both the full-circle and trimmed-arc branches
// below rely on (an oblique circle/arc really would distort into an
// ellipse under orthogonal projection, which SketchGeometry.h has no
// representation for).
bool circleAxisParallelToPlane(const gp_Circ& circ, const SketchPlane& plane) {
    const gp_Dir axis = circ.Axis().Direction();
    const double planeNormalLen =
        std::sqrt(plane.normal.x * plane.normal.x + plane.normal.y * plane.normal.y + plane.normal.z * plane.normal.z);
    const double dot = planeNormalLen > 1e-12
                          ? (axis.X() * plane.normal.x + axis.Y() * plane.normal.y + axis.Z() * plane.normal.z) / planeNormalLen
                          : 0.0;
    return std::abs(std::abs(dot) - 1.0) < 1e-6;
}

// ccw is determined empirically from the projected midpoint rather than
// reasoning about the 3D axis's own sign relative to sketch's normal
// (circleAxisParallelToPlane allows either sign) -- robust regardless of
// which way the curve's own axis points.
bool arcIsCcw(const Point2D& center, const Point2D& start, const Point2D& end, const Point2D& mid) {
    const double startAngle = std::atan2(start.y - center.y, start.x - center.x);
    const double endAngle = std::atan2(end.y - center.y, end.x - center.x);
    const double midAngle = std::atan2(mid.y - center.y, mid.x - center.x);
    const double ccwSpanToMid = positiveAngle(midAngle - startAngle);
    const double ccwSpanToEnd = positiveAngle(endAngle - startAngle);
    return ccwSpanToMid < ccwSpanToEnd;
}

bool projectEdgeImpl(Sketch& sketch, const TopoDS_Shape& shape, int edgeIndex, int tessellationSegments,
                     ExternalGeometryRef* outRef) {
    if (shape.IsNull() || edgeIndex < 0 || tessellationSegments < 1) return false;

    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
    if (edgeIndex >= edgeMap.Extent()) return false;

    const TopoDS_Edge edge = TopoDS::Edge(edgeMap(edgeIndex + 1));
    BRepAdaptor_Curve curve(edge);
    const SketchPlane& plane = sketch.placement();

    if (outRef) {
        const auto fp = fingerprintEdge(shape, edgeIndex);
        if (!fp) return false;
        *outRef = ExternalGeometryRef{};
        outRef->fingerprint = *fp;
        outRef->tessellationSegments = tessellationSegments;
    }

    if (curve.GetType() == GeomAbs_Line) {
        const Point2D a = projectToPlane(curve.Value(curve.FirstParameter()), plane);
        const Point2D b = projectToPlane(curve.Value(curve.LastParameter()), plane);
        const int pa = sketch.addPoint(a, /*fixed=*/true);
        const int pb = sketch.addPoint(b, /*fixed=*/true);
        const int lineIdx = sketch.addLine(pa, pb, /*construction=*/true);
        if (outRef) {
            outRef->kind = ExternalGeometryRef::Kind::Line;
            outRef->pointIndices = {pa, pb};
            outRef->geomIndex = lineIdx;
        }
        return true;
    }

    if (curve.GetType() == GeomAbs_Circle) {
        const gp_Circ circ = curve.Circle();
        const bool isFullCircle = std::abs((curve.LastParameter() - curve.FirstParameter()) - 2.0 * M_PI) < 1e-6;
        const bool isParallel = circleAxisParallelToPlane(circ, plane);

        if (isFullCircle && isParallel) {
            const Point2D center = projectToPlane(circ.Location(), plane);
            const int centerIdx = sketch.addPoint(center, /*fixed=*/true);
            const int circleIdx = sketch.addCircle(centerIdx, circ.Radius(), /*construction=*/true);
            if (outRef) {
                outRef->kind = ExternalGeometryRef::Kind::Circle;
                outRef->pointIndices = {centerIdx};
                outRef->geomIndex = circleIdx;
            }
            return true;
        }

        // A TRIMMED circular edge whose axis is parallel to sketch's own
        // plane, just not a complete loop -- same exact-radius projection,
        // plus a real SketchArc instead of tessellating.
        if (isParallel) {
            const Point2D center = projectToPlane(circ.Location(), plane);
            const Point2D start = projectToPlane(curve.Value(curve.FirstParameter()), plane);
            const Point2D end = projectToPlane(curve.Value(curve.LastParameter()), plane);
            const Point2D mid = projectToPlane(curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2.0), plane);
            const bool ccw = arcIsCcw(center, start, end, mid);

            const int centerIdx = sketch.addPoint(center, /*fixed=*/true);
            const int startIdx = sketch.addPoint(start, /*fixed=*/true);
            const int endIdx = sketch.addPoint(end, /*fixed=*/true);
            const int arcIdx = sketch.addArc(centerIdx, startIdx, endIdx, circ.Radius(), ccw, /*construction=*/true);
            if (outRef) {
                outRef->kind = ExternalGeometryRef::Kind::Arc;
                outRef->pointIndices = {centerIdx, startIdx, endIdx};
                outRef->geomIndex = arcIdx;
            }
            return true;
        }
    }

    if (outRef) outRef->kind = ExternalGeometryRef::Kind::Tessellated;
    tessellateIntoSketch(sketch, curve, plane, tessellationSegments, outRef);
    return true;
}
} // namespace

bool projectExternalEdge(Sketch& sketch, const TopoDS_Shape& shape, int edgeIndex, int tessellationSegments) {
    return projectEdgeImpl(sketch, shape, edgeIndex, tessellationSegments, nullptr);
}

bool projectExternalEdgeTracked(Sketch& sketch, const TopoDS_Shape& shape, int edgeIndex, ExternalGeometryRef& outRef,
                                int tessellationSegments) {
    return projectEdgeImpl(sketch, shape, edgeIndex, tessellationSegments, &outRef);
}

bool refreshExternalGeometry(Sketch& sketch, const ExternalGeometryRef& ref, const TopoDS_Shape& currentShape) {
    if (currentShape.IsNull()) return false;
    const int edgeIndex = resolveEdgeIndex(currentShape, ref.fingerprint);
    if (edgeIndex < 0) return false;

    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(currentShape, TopAbs_EDGE, edgeMap);
    if (edgeIndex >= edgeMap.Extent()) return false;

    const TopoDS_Edge edge = TopoDS::Edge(edgeMap(edgeIndex + 1));
    BRepAdaptor_Curve curve(edge);
    const SketchPlane& plane = sketch.placement();
    auto& points = sketch.points();

    const auto pointIndexValid = [&](int idx) {
        return idx >= 0 && idx < static_cast<int>(points.size());
    };

    switch (ref.kind) {
    case ExternalGeometryRef::Kind::Line: {
        if (curve.GetType() != GeomAbs_Line || ref.pointIndices.size() != 2) return false;
        if (!pointIndexValid(ref.pointIndices[0]) || !pointIndexValid(ref.pointIndices[1])) return false;
        points[static_cast<std::size_t>(ref.pointIndices[0])] = projectToPlane(curve.Value(curve.FirstParameter()), plane);
        points[static_cast<std::size_t>(ref.pointIndices[1])] = projectToPlane(curve.Value(curve.LastParameter()), plane);
        return true;
    }
    case ExternalGeometryRef::Kind::Circle: {
        if (curve.GetType() != GeomAbs_Circle || ref.pointIndices.size() != 1 || ref.geomIndex < 0) return false;
        const gp_Circ circ = curve.Circle();
        const bool isFullCircle = std::abs((curve.LastParameter() - curve.FirstParameter()) - 2.0 * M_PI) < 1e-6;
        if (!isFullCircle || !circleAxisParallelToPlane(circ, plane)) return false;
        if (!pointIndexValid(ref.pointIndices[0])) return false;
        if (ref.geomIndex >= static_cast<int>(sketch.circles().size())) return false;
        points[static_cast<std::size_t>(ref.pointIndices[0])] = projectToPlane(circ.Location(), plane);
        sketch.circles()[static_cast<std::size_t>(ref.geomIndex)].radius = circ.Radius();
        return true;
    }
    case ExternalGeometryRef::Kind::Arc: {
        if (curve.GetType() != GeomAbs_Circle || ref.pointIndices.size() != 3 || ref.geomIndex < 0) return false;
        const gp_Circ circ = curve.Circle();
        const bool isFullCircle = std::abs((curve.LastParameter() - curve.FirstParameter()) - 2.0 * M_PI) < 1e-6;
        if (isFullCircle || !circleAxisParallelToPlane(circ, plane)) return false;
        for (int idx : ref.pointIndices) {
            if (!pointIndexValid(idx)) return false;
        }
        if (ref.geomIndex >= static_cast<int>(sketch.arcs().size())) return false;

        const Point2D center = projectToPlane(circ.Location(), plane);
        const Point2D start = projectToPlane(curve.Value(curve.FirstParameter()), plane);
        const Point2D end = projectToPlane(curve.Value(curve.LastParameter()), plane);
        const Point2D mid = projectToPlane(curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2.0), plane);

        points[static_cast<std::size_t>(ref.pointIndices[0])] = center;
        points[static_cast<std::size_t>(ref.pointIndices[1])] = start;
        points[static_cast<std::size_t>(ref.pointIndices[2])] = end;
        SketchArc& arc = sketch.arcs()[static_cast<std::size_t>(ref.geomIndex)];
        arc.radius = circ.Radius();
        arc.ccw = arcIsCcw(center, start, end, mid);
        return true;
    }
    case ExternalGeometryRef::Kind::Tessellated: {
        // Reject only if the resolved edge would now get one of the
        // EXACT representations instead (a Line, or a Circle whose axis
        // is now parallel to the plane) -- a still-non-parallel circle is
        // exactly the case this Tessellated ref was already handling, not
        // a mismatch.
        if (curve.GetType() == GeomAbs_Line) return false;
        if (curve.GetType() == GeomAbs_Circle && circleAxisParallelToPlane(curve.Circle(), plane)) return false;
        if (static_cast<int>(ref.pointIndices.size()) != ref.tessellationSegments + 1) return false;
        for (int idx : ref.pointIndices) {
            if (!pointIndexValid(idx)) return false;
        }
        const double u1 = curve.FirstParameter();
        const double u2 = curve.LastParameter();
        for (std::size_t s = 0; s < ref.pointIndices.size(); ++s) {
            const double u = u1 + (u2 - u1) * (static_cast<double>(s) / ref.tessellationSegments);
            points[static_cast<std::size_t>(ref.pointIndices[s])] = projectToPlane(curve.Value(u), plane);
        }
        return true;
    }
    }
    return false;
}

} // namespace lcad
