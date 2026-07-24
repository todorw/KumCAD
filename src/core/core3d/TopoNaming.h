#pragma once

#include "core/core3d/Fingerprint.h"
#include "core/sketch/SketchGeometry.h"

#include <TopoDS_Shape.hxx>

#include <optional>

namespace lcad {

// Geometric-fingerprint edge/face re-identification, captured at
// selection time so Fillet/Chamfer/Shell can re-find "the same" edge/
// face after an upstream recompute reshuffles OCCT's own edge/face
// ordering -- the topological naming problem every B-rep kernel has:
// OCCT gives no guarantee that TopExp::MapShapes returns edges/faces in
// the same order after a shape is rebuilt with different parameters,
// even for what a user would call "the same" edge.
//
// This is a real, disclosed MITIGATION, not a solution: a nearest-
// geometry match (midpoint+length for edges, centroid+area for faces),
// not FreeCAD's own much deeper (and still imperfect) topological
// naming / Link ElementMap system. It can still mis-resolve two edges
// that happen to share a very similar midpoint+length (e.g. two
// opposite edges of a symmetric solid) -- a real, accepted limitation
// of a from-scratch nearest-match approach, not attempted to be solved
// exactly here. See Fingerprint.h for the two plain fingerprint structs
// themselves.

// Computes the fingerprint of the edge/face at index (0-based into
// TopExp::MapShapes(shape, TopAbs_EDGE/FACE, ...)'s own ordering -- the
// same numbering Pick3D.h's pickEdge/pickFace return, and the same
// numbering Feature3D::edgeIndices/faceIndices already use). Returns
// nullopt if index is out of range.
std::optional<EdgeFingerprint> fingerprintEdge(const TopoDS_Shape& shape, int index);
std::optional<FaceFingerprint> fingerprintFace(const TopoDS_Shape& shape, int index);

// Re-finds the index of the edge/face in shape's CURRENT ordering whose
// fingerprint most closely matches target (nearest midpoint/centroid
// distance; ties broken by closest length/area) -- a best-effort
// nearest match, not a threshold-gated fail: always returns some valid
// index if shape has at least one edge/face, since "closest available"
// degrades more gracefully for a live recompute than refusing outright.
// Returns -1 only if shape has no edges/faces at all.
int resolveEdgeIndex(const TopoDS_Shape& shape, const EdgeFingerprint& target);
int resolveFaceIndex(const TopoDS_Shape& shape, const FaceFingerprint& target);

// Builds a sketch plane sitting exactly on the planar face at index (same
// numbering as fingerprintFace/pickFace). origin/xAxis come straight from
// the face's own underlying gp_Pln position (OCCT's own canonical in-plane
// reference frame for a planar surface, not an arbitrary/rotating choice),
// so re-attaching to "the same" face after a rebuild reproduces the same
// in-plane axes. normal is flipped for a TopAbs_REVERSED face so it always
// points away from the solid's own material, matching pickFace's own
// outward-normal convention. Returns nullopt if index is out of range or
// the face isn't planar (BRepAdaptor_Surface's GeomAbs_Plane check) -- a
// real, disclosed limitation: only flat faces can host a sketch plane,
// the same restriction FreeCAD's own simple face attachment has.
std::optional<SketchPlane> planeFromFace(const TopoDS_Shape& shape, int index);

// A point + unit direction derived from a STRAIGHT edge of shape at index
// (same numbering as pickEdge/fingerprintEdge) -- point is the edge's own
// first vertex, direction runs from it toward the edge's other vertex.
// Lets Mirror/LinearPattern/PolarPattern/Revolve's own posX/Y/Z+dirX/Y/Z
// fields be set by picking a real edge (an existing feature's own side,
// say) instead of typing raw numbers, the same "real geometry drives the
// parameter" idea planeFromFace already gives sketch attachment. Returns
// nullopt if index is out of range or the edge isn't a straight line
// (BRepAdaptor_Curve's GeomAbs_Line check) -- a real, disclosed
// limitation: an arc/spline edge has no single direction to derive.
struct EdgeAxis {
    double pointX = 0.0, pointY = 0.0, pointZ = 0.0;
    double dirX = 0.0, dirY = 0.0, dirZ = 1.0;
};
std::optional<EdgeAxis> axisFromEdge(const TopoDS_Shape& shape, int index);

// The exact position of a real vertex of shape at index (0-based into
// TopExp::MapShapes(shape, TopAbs_VERTEX, ...)'s own ordering -- there's
// no dedicated vertex-picking in Pick3D.h yet, so this indexes the same
// way edge/face picking already does, for a caller that already knows
// which vertex it wants, e.g. from inspecting the shape directly). Lets
// Hole/Pattern/Mirror's own posX/Y/Z be set from a real picked corner
// instead of typed numbers, the same idea axisFromEdge/planeFromFace
// already give edges/faces. Returns nullopt if index is out of range.
struct VertexPoint {
    double x = 0.0, y = 0.0, z = 0.0;
};
std::optional<VertexPoint> pointFromVertex(const TopoDS_Shape& shape, int index);

// The center point of a CIRCULAR edge of shape at index (same numbering
// as pickEdge/fingerprintEdge/axisFromEdge) -- the everyday "place a
// second hole at the same center as an existing one" or "pattern around
// this hole's own axis" workflow, picking a hole's own rim rather than
// typing its center by hand. normal is the circle's own plane normal
// (consistent orientation from OCCT's own underlying curve, not
// guaranteed outward-vs-inward -- a real, disclosed limitation, same
// caveat planeFromFace's REVERSED-flip doesn't apply to a bare edge,
// which carries no solid-material side to be "outward" from). Returns
// nullopt if index is out of range or the edge isn't circular
// (BRepAdaptor_Curve's GeomAbs_Circle check).
struct EdgeCircle {
    double centerX = 0.0, centerY = 0.0, centerZ = 0.0;
    double normalX = 0.0, normalY = 0.0, normalZ = 1.0;
    double radius = 0.0;
};
std::optional<EdgeCircle> centerOfCircularEdge(const TopoDS_Shape& shape, int index);

} // namespace lcad
