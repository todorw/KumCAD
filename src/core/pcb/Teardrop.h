#pragma once

#include "core/Ids.h"
#include "core/geometry/Point2D.h"

#include <vector>

namespace lcad {

class Document;

// Teardrop fill polygon connecting a track of trackWidth to a round pad/
// via of padRadius, widening the connection near the pad to reduce
// stress concentration and improve manufacturing yield at acute track-
// pad junctions (KiCad 7+'s own teardrop feature). `dir` is the unit
// vector from padCenter toward the track (the track's own direction
// leaving the pad). The pad-side shoulders sit where a straight line
// from the track's own edge is truly TANGENT to the pad's circle
// (standard tangent-line-from-an-external-point geometry: in the right
// triangle formed by the pad center, the tangent point, and the track
// edge point, the angle at the pad center is acos(padRadius/distance)),
// not a fixed angle -- a real, exact construction rather than an
// approximation. halfAngleDegrees now only matters as a fallback/cap:
// used directly when the track edge point isn't actually outside the
// pad circle (an unusually wide track on a small pad, where no real
// tangent line exists), and as an upper bound the computed tangent
// angle can't exceed (an extreme width/radius ratio can otherwise push
// the shoulder past a half-turn and self-intersect). arcSamples points
// are interpolated around the pad's own circle between the two
// shoulders so the fill hugs the pad's curvature instead of cutting a
// straight chord across it.
//
// Returns empty if padRadius <= 0, trackWidth <= 0, length <= padRadius
// (the teardrop's far edge must clear the pad), or dir is degenerate.
std::vector<Point2D> buildTeardrop(const Point2D& padCenter, double padRadius, const Point2D& dir, double trackWidth,
                                   double length, double halfAngleDegrees = 45.0, int arcSamples = 6);

struct TeardropParams {
    double halfAngleDegrees = 45.0; // fallback/cap only now -- see buildTeardrop's own comment
    double lengthFactor = 2.0; // the teardrop's far edge sits lengthFactor*padRadius from the pad center
    double tolerance = 0.05;   // how close a track endpoint must land on a pad/via to get a teardrop
    int arcSamples = 6;
};

// Scans doc for every TrackEntity endpoint landing on a footprint pad or
// a via (within params.tolerance) and adds a teardrop fill (a solid
// HatchEntity, same representation buildCopperPourWithClearance's pieces
// use) there. Skips a track too short to have a real direction (needs at
// least 2 distinct vertices). Returns the ids of every teardrop added,
// on layer.
std::vector<EntityId> addTeardropsToDocument(Document& doc, LayerId layer, const TeardropParams& params = {});

} // namespace lcad
