#pragma once

#include "core/Ids.h"
#include "core/geometry/Point2D.h"

#include <vector>

namespace lcad {

class Document;

// Places ground/net-tie vias at even arc-length intervals along a closed
// copper pour's own boundary polygon -- KiCad's own "Add Vias" stitching
// helper, tying a ground/power pour through to the same net on another
// layer for return-current and thermal reasons.
//
// Each via is inset from the boundary edge (toward the polygon's own
// centroid) by inset, so its copper ring lands inside the pour instead of
// hanging half off the edge. Real, disclosed simplification: the inset is
// a per-vertex move toward the centroid, not a true parallel polygon
// offset (the same "no general 2D polygon-boolean/clipping library" call
// CopperPour.h's own header already discloses) -- fine for the convex,
// largely-rectangular board-edge/keepout boundaries this is meant for,
// but a deeply concave boundary can place a via outside the actual pour.
// Doesn't itself check DRC clearance against other-net copper -- run
// runDrc() (Drc.h) afterward.
//
// Returns the ids of every ViaEntity placed (on layer). Returns an empty
// vector if boundary has fewer than 3 points, spacing <= 0, or the
// resulting inset boundary degenerates to zero perimeter.
std::vector<EntityId> stitchVias(Document& doc, LayerId layer, const std::vector<Point2D>& boundary, double spacing,
                                 double inset = 1.0, double diameter = 0.6, double drillDiameter = 0.3);

} // namespace lcad
