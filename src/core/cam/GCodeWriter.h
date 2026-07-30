#pragma once

#include "core/cam/Toolpath.h"

#include <string>

namespace lcad {

// Writes toolpath as G-code: G21/G90 header, then one full lap of the
// toolpath per depth pass -- retract to safeHeight, rapid to the start
// point, plunge at plungeRate, G1 moves along every vertex at feedRate --
// stepping from stepDown down to cutDepth in equal increments (last pass
// clamped to exactly cutDepth), or a single pass at cutDepth when stepDown
// is <=0 or >= cutDepth. Ends with a final retract and M30. This is a
// plain, common-subset G-code dialect (no tool-change codes, no arcs --
// toolpath is already a flattened polyline), not tuned to any specific
// controller's quirks. Returns false (with *errorOut set, if provided) on
// a file-open failure, or if toolpath has fewer than 2 points.
bool writeGCode(const std::vector<Point2D>& toolpath, const ToolpathParams& params, const std::string& path,
                std::string* errorOut = nullptr);

} // namespace lcad
