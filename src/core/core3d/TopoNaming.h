#pragma once

#include "core/core3d/Fingerprint.h"

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

} // namespace lcad
