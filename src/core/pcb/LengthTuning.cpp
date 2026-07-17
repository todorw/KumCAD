#include "core/pcb/LengthTuning.h"

#include <algorithm>
#include <cmath>

namespace lcad {

double pathLength(const std::vector<Point2D>& path) {
    double total = 0.0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) total += path[i].distanceTo(path[i + 1]);
    return total;
}

std::vector<Point2D> meanderSegment(const Point2D& a, const Point2D& b, double targetExtraLength, double amplitude,
                                    double pitch, double* achievedExtraLength) {
    if (achievedExtraLength) *achievedExtraLength = 0.0;
    if (targetExtraLength <= 0.0 || amplitude <= 0.0 || pitch <= 0.0) return {a, b};

    const Point2D delta = b - a;
    const double footprint = delta.length();
    if (footprint < pitch) return {a, b}; // not even room for one tooth

    const Point2D dir = delta * (1.0 / footprint);
    const Point2D perp(-dir.y, dir.x);

    // Extra length contributed by one tooth: two diagonal legs of length
    // sqrt((pitch/2)^2 + amplitude^2) each, replacing a straight run of
    // length `pitch`.
    const double toothLength = 2.0 * std::sqrt((pitch / 2.0) * (pitch / 2.0) + amplitude * amplitude);
    const double extraPerTooth = toothLength - pitch;
    if (extraPerTooth <= 1e-12) return {a, b}; // degenerate (amplitude ~0)

    const int maxTeeth = static_cast<int>(std::floor(footprint / pitch));
    if (maxTeeth <= 0) return {a, b};

    int teeth = static_cast<int>(std::ceil(targetExtraLength / extraPerTooth));
    teeth = std::clamp(teeth, 1, maxTeeth);

    const double meanderWidth = teeth * pitch;
    const double leadIn = (footprint - meanderWidth) / 2.0;

    std::vector<Point2D> path;
    path.reserve(2 + teeth + 1);
    path.push_back(a);
    Point2D cursor = a + dir * leadIn;
    if (leadIn > 1e-9) path.push_back(cursor);

    for (int t = 0; t < teeth; ++t) {
        const double sign = (t % 2 == 0) ? 1.0 : -1.0;
        const Point2D peak = cursor + dir * (pitch / 2.0) + perp * (amplitude * sign);
        path.push_back(peak);
        cursor = cursor + dir * pitch;
        path.push_back(cursor);
    }

    if (footprint - leadIn - meanderWidth > 1e-9) path.push_back(b);
    else path.back() = b; // snap the final centerline point exactly onto b

    if (achievedExtraLength) *achievedExtraLength = teeth * extraPerTooth;
    return path;
}

TuneResult tuneTrackLength(const std::vector<Point2D>& path, double targetLength, double amplitude, double pitch) {
    TuneResult result;
    result.originalLength = pathLength(path);
    result.path = path;
    result.achievedLength = result.originalLength;

    if (result.originalLength >= targetLength || path.size() < 2) {
        result.metTarget = result.originalLength >= targetLength;
        return result;
    }

    // Pick the longest consecutive-vertex segment to meander.
    std::size_t longestIndex = 0;
    double longestLength = 0.0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const double len = path[i].distanceTo(path[i + 1]);
        if (len > longestLength) {
            longestLength = len;
            longestIndex = i;
        }
    }

    const double needed = targetLength - result.originalLength;
    double achievedExtra = 0.0;
    const std::vector<Point2D> meandered =
        meanderSegment(path[longestIndex], path[longestIndex + 1], needed, amplitude, pitch, &achievedExtra);

    std::vector<Point2D> tuned;
    tuned.insert(tuned.end(), path.begin(), path.begin() + static_cast<long>(longestIndex));
    tuned.insert(tuned.end(), meandered.begin(), meandered.end());
    tuned.insert(tuned.end(), path.begin() + static_cast<long>(longestIndex) + 2, path.end());

    result.path = tuned;
    result.achievedLength = result.originalLength + achievedExtra;
    result.metTarget = result.achievedLength >= targetLength - 1e-6;
    return result;
}

} // namespace lcad
