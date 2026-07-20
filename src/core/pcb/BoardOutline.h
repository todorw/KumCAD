#pragma once

#include "core/Ids.h"
#include "core/geometry/Point2D.h"

#include <optional>
#include <string>
#include <vector>

namespace lcad {

class Document;

// Even-odd ray-casting point-in-polygon test, shared by every module that
// needs one (CopperPour, Autorouter's keepout check) instead of each
// carrying its own private copy.
bool pointInPolygon(const Point2D& p, const std::vector<Point2D>& poly);

// Reads layerName's (default "Edge.Cuts", matching KiCad's own reserved
// board-outline layer name) closed geometry into a plain boundary polygon
// -- the same std::vector<Point2D> shape every board-boundary-consuming
// function here (Board3D, CopperPour, Autorouter's keepouts, Panelization,
// SpecctraWriter) already takes as a hand-passed parameter, so this just
// derives it from the drawing instead of requiring the caller to already
// know it.
//
// Standalone LineEntity/ArcEntity segments on the layer are chained by
// endpoint proximity (within tolerance) into closed loops, the same
// "coincidence is structural, checked by position" idea
// DocumentConstraints.cpp already uses for merging points -- and a closed
// PolylineEntity on the layer is a complete loop on its own, no chaining
// needed. Arcs are tessellated (32 segments) into the returned polygon.
// If several closed loops exist (e.g. a board with an internal cutout
// also drawn on Edge.Cuts), the one with the largest enclosed area is
// returned as the OUTER boundary -- matching SketchToFace's own
// "largest loop wins" convention -- with no support yet for returning
// the smaller loops as holes (a real, disclosed gap: every consumer here
// already only takes one boundary polygon, not a boundary-plus-holes
// shape).
//
// Returns an empty vector if the layer doesn't exist, has no geometry, or
// none of its geometry closes into a loop.
std::vector<Point2D> deriveBoardOutline(const Document& doc, const std::string& layerName = "Edge.Cuts",
                                        double chainTolerance = 1e-6);

// A no-go region (KiCad's own "Add Keepout Area"/rule area with copper-
// pour and routing restrictions checked): polygon lives in the SAME
// world-space coordinates as a board outline. layer left empty means the
// zone applies on every layer, matching a keepout area KiCad itself
// marks "all layers" rather than restricting it to one -- set it to
// apply the zone to a single layer only instead.
struct KeepoutZone {
    std::vector<Point2D> polygon;
    std::optional<LayerId> layer;
    bool blocksCopperPour = true;
    bool blocksAutorouting = true;
};

// True if p (on layer) falls inside any zone in zones whose own
// restriction (blocksCopperPour if forPour, else blocksAutorouting) is
// set and whose own layer (if restricted to one) matches layer.
bool pointInKeepout(const Point2D& p, LayerId layer, const std::vector<KeepoutZone>& zones, bool forPour);

} // namespace lcad
