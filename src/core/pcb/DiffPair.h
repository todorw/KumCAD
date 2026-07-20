#pragma once

#include "core/geometry/Point2D.h"
#include "core/pcb/NetClass.h"

#include <vector>

namespace lcad {

// Differential pair routing: given a centerline path the caller already
// picked (e.g. via the normal click-based track tool -- there's no
// interactive fanout/via handling here, a real, disclosed simplification
// vs. a real push-and-shove diff-pair router), builds two parallel tracks
// offset +-gap/2 from it, each connected to its own net's real pad
// positions by a straight stub. Reuses core/geometry/PolylineOps.h's
// offsetPolyline for the actual parallel-copy geometry (miter-normal
// corners, exact for line-line joints), rather than writing new offset
// math -- the same primitive OFFSET's own command already uses.
struct DiffPairResult {
    bool ok = false; // false if the centerline degenerates (fewer than 2 points, or offsetPolyline fails)
    std::vector<Point2D> pPath;
    std::vector<Point2D> nPath;
    double pLength = 0.0;
    double nLength = 0.0;
    bool lengthMatched = false; // true once the shorter leg was meandered to reach the longer leg's length
};

// pStart/pEnd are the P net's two real pad positions, nStart/nEnd the N
// net's; the function figures out which side of the centerline (+gap/2 or
// -gap/2) is actually closer to the P pads, so callers don't need to know
// the offset's handedness convention. When autoMatchLength is true (the
// default), the shorter final leg is meandered (core/pcb/LengthTuning.h)
// to match the longer one; meander amplitude/pitch are derived from gap
// (amplitude = 1.5*gap, pitch = 3*gap) so the teeth stay clear of the
// OTHER track in the pair by construction, not independently tunable in
// this pass.
DiffPairResult routeDiffPair(const std::vector<Point2D>& centerline, double gap, const Point2D& pStart,
                             const Point2D& pEnd, const Point2D& nStart, const Point2D& nEnd,
                             bool autoMatchLength = true);

// Same as above, but gap comes from netClass.diffPairGap (see NetClass.h)
// instead of a directly-passed value -- real per-net-class diff-pair
// rules (a "USB" class routing a tighter/looser gap than "Default"),
// mirroring the findNetClass-then-override pattern autoroute() and
// runDrc() already use for trackWidth/clearance. netClass.diffPairWidth
// is NOT applied here (this function only computes the pair's own
// geometry, not track width -- see routeDiffPair's own header comment);
// callers building the actual TrackEntity pair should read
// netClass.diffPairWidth themselves the same way they already choose
// pPath/nPath's width today.
DiffPairResult routeDiffPair(const std::vector<Point2D>& centerline, const NetClass& netClass, const Point2D& pStart,
                             const Point2D& pEnd, const Point2D& nStart, const Point2D& nEnd,
                             bool autoMatchLength = true);

} // namespace lcad
