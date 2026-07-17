#pragma once

#include "core/geometry/Polyline.h"

#include <memory>
#include <vector>

namespace lcad {

class Entity;

// Parallel copy of a polyline at the given distance, on the side of
// sidePoint: vertices move along miter (angle-bisector) normals, bulges are
// preserved (an arc's included angle doesn't change under concentric offset).
// Exact for tangent-continuous polylines and line-line corners; sharp
// arc-line corners are approximate. Returns nullptr when the offset
// degenerates (e.g. an inward offset larger than a local arc radius).
std::unique_ptr<PolylineEntity> offsetPolyline(const PolylineEntity& source, EntityId newId, double distance,
                                               const Point2D& sidePoint);

// PEDIT Join: chains lines, arcs, and open polylines whose endpoints touch
// (within tol) into one polyline, preserving arcs as bulges. Pieces are
// reversed as needed. Returns nullptr unless every part joins into a single
// chain; the result is closed when the chain's ends meet.
std::unique_ptr<PolylineEntity> joinToPolyline(EntityId newId, LayerId layer,
                                               const std::vector<const Entity*>& parts, double tol = 1e-6);

// REVCLOUD: resamples boundary (already-flattened points, e.g. a picked
// polyline's own vertices or a tessellated circle) at even arc-length
// intervals of approximately archLength, and gives every resulting segment
// an outward bulge -- a real polyline of arc segments, the same
// representation AutoCAD's own revision cloud produces. Bulge direction
// (which way is "outward") is read once from boundary's own overall
// signed area (shoelace), then applied uniformly -- a real, disclosed
// simplification for a simple (non-self-intersecting) boundary, not a
// per-segment normal computation. Returns nullptr if boundary has fewer
// than 3 points or archLength <= 0.
std::unique_ptr<PolylineEntity> revisionCloud(EntityId newId, LayerId layer, const std::vector<Point2D>& boundary,
                                              bool closed, double archLength, double bulgeMagnitude = 0.4);

} // namespace lcad
