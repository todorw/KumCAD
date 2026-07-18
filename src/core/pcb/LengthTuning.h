#pragma once

#include "core/geometry/Point2D.h"

#include <vector>

namespace lcad {

// KiCad's track length tuning tool (a serpentine/meander pattern that adds
// electrical length without changing a track's physical footprint, used
// to length-match clock/data/differential-pair traces). A real interactive
// tuner lets the user hand-place and drag the meander region; this instead
// automatically meanders the LONGEST straight segment of the given path
// (a real, disclosed simplification, not full interactive placement).

// Total path length (sum of consecutive vertex distances).
double pathLength(const std::vector<Point2D>& path);

// Replaces the straight run a->b with a zigzag of alternating triangular
// teeth (each tooth: centerline -> peak (perpendicular offset +-amplitude,
// alternating sign per tooth) -> centerline, over longitudinal width
// pitch) that adds extra path length while keeping the same two
// endpoints and the same physical footprint (the teeth fit within
// |b-a|). Teeth are centered on the segment, with straight lead-in/
// lead-out on either side. Returns just {a, b} unchanged if
// targetExtraLength <= 0, amplitude <= 0, pitch <= 0, or the segment is
// too short to fit even one tooth.
//
// achievedExtraLength (if non-null) receives how much extra length was
// actually added. When the footprint can't fit enough teeth at the
// requested amplitude to reach targetExtraLength, amplitude itself grows
// (using every available tooth, wider) to make up the difference instead
// of falling short -- real, disclosed departure from a fixed-style
// meander: there's no upper bound on how far amplitude grows for a very
// large target, and no clearance awareness of neighboring copper, so the
// caller should sanity-check the amplitude actually used stays realistic.
std::vector<Point2D> meanderSegment(const Point2D& a, const Point2D& b, double targetExtraLength, double amplitude,
                                    double pitch, double* achievedExtraLength = nullptr);

struct TuneResult {
    std::vector<Point2D> path;
    double originalLength = 0.0;
    double achievedLength = 0.0;
    // false only if the longest segment can't fit even one tooth at all
    // (shorter than pitch) -- meanderSegment grows amplitude to reach the
    // target otherwise, so a segment with room for at least one tooth
    // always meets it now.
    bool metTarget = false;
};

// Tunes path to targetLength by meandering its single longest straight
// segment (consecutive-vertex run). If path is already at or past
// targetLength, returns it unchanged (a trace can only be lengthened by
// meandering, never shortened this way -- matching a real tuner, which
// also can't shrink a trace).
TuneResult tuneTrackLength(const std::vector<Point2D>& path, double targetLength, double amplitude, double pitch);

} // namespace lcad
