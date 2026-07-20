#include "core/pcb/DiffPair.h"

#include "core/geometry/Polyline.h"
#include "core/geometry/PolylineOps.h"
#include "core/pcb/LengthTuning.h"

#include <algorithm>
#include <cmath>

namespace lcad {

namespace {

// Prepends realStart and appends realEnd to offsetPath, orienting
// offsetPath (and skipping a duplicate point where a stub is ~zero-length)
// so realStart lands nearest offsetPath's own first vertex.
std::vector<Point2D> buildLeg(std::vector<Point2D> offsetPath, const Point2D& realStart, const Point2D& realEnd) {
    const double natural =
        realStart.distanceTo(offsetPath.front()) + realEnd.distanceTo(offsetPath.back());
    const double reversed =
        realStart.distanceTo(offsetPath.back()) + realEnd.distanceTo(offsetPath.front());
    if (reversed < natural) std::reverse(offsetPath.begin(), offsetPath.end());

    constexpr double kEpsilon = 1e-9;
    std::vector<Point2D> leg;
    if (realStart.distanceTo(offsetPath.front()) > kEpsilon) leg.push_back(realStart);
    leg.insert(leg.end(), offsetPath.begin(), offsetPath.end());
    if (realEnd.distanceTo(leg.back()) > kEpsilon) leg.push_back(realEnd);
    return leg;
}

} // namespace

DiffPairResult routeDiffPair(const std::vector<Point2D>& centerline, double gap, const Point2D& pStart,
                             const Point2D& pEnd, const Point2D& nStart, const Point2D& nEnd,
                             bool autoMatchLength) {
    DiffPairResult result;
    if (centerline.size() < 2 || gap <= 0.0) return result;

    const PolylineEntity source(0, 0, centerline, false);

    // An arbitrary point well off to one side of the first segment, just
    // to tell offsetPolyline which handedness to use -- its own distance
    // doesn't matter, only which side of the polyline it falls on.
    const Point2D dir = (centerline[1] - centerline[0]);
    const double dirLen = dir.length();
    if (dirLen < 1e-9) return result;
    const Point2D perp(-dir.y / dirLen, dir.x / dirLen);
    const Point2D sideA = centerline.front() + perp * 1000.0;
    const Point2D sideB = centerline.front() - perp * 1000.0;

    auto offsetA = offsetPolyline(source, 1, gap / 2.0, sideA);
    auto offsetB = offsetPolyline(source, 2, gap / 2.0, sideB);
    if (!offsetA || !offsetB) return result;

    // Whichever offset's own start vertex sits closer to pStart is the P
    // side; the other is N -- robust to whatever offset handedness
    // offsetPolyline happens to use internally.
    const bool aIsP = pStart.distanceTo(offsetA->vertices().front()) <= pStart.distanceTo(offsetB->vertices().front());
    const PolylineEntity& pSource = aIsP ? *offsetA : *offsetB;
    const PolylineEntity& nSource = aIsP ? *offsetB : *offsetA;

    result.pPath = buildLeg(pSource.vertices(), pStart, pEnd);
    result.nPath = buildLeg(nSource.vertices(), nStart, nEnd);
    result.pLength = pathLength(result.pPath);
    result.nLength = pathLength(result.nPath);

    if (autoMatchLength && std::abs(result.pLength - result.nLength) > 1e-9) {
        const double target = std::max(result.pLength, result.nLength);
        const double amplitude = gap * 1.5;
        const double pitch = gap * 3.0;
        if (result.pLength < target) {
            const TuneResult tuned = tuneTrackLength(result.pPath, target, amplitude, pitch);
            result.pPath = tuned.path;
            result.pLength = tuned.achievedLength;
            result.lengthMatched = tuned.metTarget;
        } else if (result.nLength < target) {
            const TuneResult tuned = tuneTrackLength(result.nPath, target, amplitude, pitch);
            result.nPath = tuned.path;
            result.nLength = tuned.achievedLength;
            result.lengthMatched = tuned.metTarget;
        } else {
            result.lengthMatched = true;
        }
    } else {
        result.lengthMatched = true; // already equal (or matching wasn't requested)
    }

    result.ok = true;
    return result;
}

DiffPairResult routeDiffPair(const std::vector<Point2D>& centerline, const NetClass& netClass, const Point2D& pStart,
                             const Point2D& pEnd, const Point2D& nStart, const Point2D& nEnd, bool autoMatchLength) {
    return routeDiffPair(centerline, netClass.diffPairGap, pStart, pEnd, nStart, nEnd, autoMatchLength);
}

} // namespace lcad
