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
// "largest loop wins" convention. Every other loop found on the layer is
// silently dropped by this plain-polygon overload -- see
// deriveBoardOutlineWithHoles below for a caller that actually needs
// those as real cutouts, not just an equal-priority outer boundary.
//
// Returns an empty vector if the layer doesn't exist, has no geometry, or
// none of its geometry closes into a loop.
std::vector<Point2D> deriveBoardOutline(const Document& doc, const std::string& layerName = "Edge.Cuts",
                                        double chainTolerance = 1e-6);

// deriveBoardOutline's own outer boundary, plus every OTHER closed loop
// found on the same layer as a hole -- a board cutout (a slot for a
// connector, a mounting cutout) drawn as a second closed shape on
// Edge.Cuts, the same reserved-layer convention deriveKeepoutZones
// already treats every one of its own loops as individually significant
// under, rather than deriveBoardOutline's "largest wins, rest dropped"
// simplification. No containment check is performed (every non-largest
// loop is assumed a hole in the largest one) -- correct for the real
// "board shape plus cutouts" case this exists for, not a general
// nested-shapes solver.
struct BoardOutlineWithHoles {
    std::vector<Point2D> boundary;
    std::vector<std::vector<Point2D>> holes;
};
BoardOutlineWithHoles deriveBoardOutlineWithHoles(const Document& doc, const std::string& layerName = "Edge.Cuts",
                                                  double chainTolerance = 1e-6);

// True if p falls inside outline.boundary and NOT inside any of its
// holes (even-odd pointInPolygon test against each) -- "is this point
// actually part of the board," accounting for cutouts.
bool pointOnBoard(const Point2D& p, const BoardOutlineWithHoles& outline);

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

// Reads every closed loop on layerName's own geometry (default "Keepout",
// a reserved layer name matching how deriveBoardOutline above already
// treats "Edge.Cuts" -- a real keepout area drawn as ordinary closed
// Polyline/Line+Arc geometry, no dedicated entity type needed) into one
// KeepoutZone per loop, blocking both copper pour and autorouting on
// every layer. ALSO scans two more reserved conventions the same way:
//   - layerName + ".NoPour" / ".NoRoute" restricts a zone to just one
//     restriction, matching real KiCad's own separate "no copper pour"/
//     "no tracks or vias" checkboxes on a rule area.
//   - layerName + "." + <any other real layer's own name> (e.g.
//     "Keepout.F.Cu") restricts a zone to that single copper layer
//     instead of every layer, blocking both pour and routing there --
//     matching real KiCad's own per-layer rule-area restriction. Combining
//     this with the pour-only/route-only split above (e.g.
//     "Keepout.F.Cu.NoPour") isn't modeled -- a real, disclosed remaining
//     scope limit.
// Unlike deriveBoardOutline, every closed loop found becomes its own
// zone rather than just the largest -- a board can have several
// independent keepout areas.
std::vector<KeepoutZone> deriveKeepoutZones(const Document& doc, const std::string& layerName = "Keepout",
                                            double chainTolerance = 1e-6);

// True if p (on layer) falls inside any zone in zones whose own
// restriction (blocksCopperPour if forPour, else blocksAutorouting) is
// set and whose own layer (if restricted to one) matches layer.
bool pointInKeepout(const Point2D& p, LayerId layer, const std::vector<KeepoutZone>& zones, bool forPour);

} // namespace lcad
