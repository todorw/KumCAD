#pragma once

#include "core/cam/Toolpath.h"
#include "core/geometry/Point2D.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace lcad {

struct Cam3DParams {
    double toolDiameter = 6.0;
    CutSide side = CutSide::Outside;
    double feedRate = 800.0;
    double plungeRate = 200.0;
    double stepDown = 2.0;    // Z distance between roughing levels
    double safeHeight = 10.0; // absolute Z retract height, above the stock top
};

struct Cam3DLevel {
    double z = 0.0;
    std::vector<Point2D> toolpath; // already tool-radius-compensated (see core/cam/Toolpath.h)
};

// "3D CAM" here means slice-based multi-level roughing, not full 3-axis
// surface finishing (a ball-nose tool following a solid's actual curved
// surfaces) -- that's a much deeper computational-geometry undertaking,
// flagged as one of the plan's riskiest items when Phase 3 was scoped.
// This slices shape with a horizontal plane at every Z from its top down
// to its bottom, params.stepDown apart (plus one final pass exactly at the
// bottom), and at each level keeps only the LARGEST closed loop the slice
// produces (same "largest loop is the outer boundary" heuristic as
// SketchToFace.cpp) -- so a slice through a part with an island or a
// pocket only mills the outer envelope at that level, not the island/
// pocket detail. That's a disclosed, real scope cut, not a bug.
std::vector<Cam3DLevel> sliceIntoLevels(const TopoDS_Shape& shape, const Cam3DParams& params);

// Writes every level as one G-code program, machining top to bottom, in
// the same plain-dialect conventions as core/cam/GCodeWriter.h (G21/G90
// header, retract to params.safeHeight between levels, M30 end). Returns
// false (with *errorOut set, if given) if levels is empty or the file
// can't be opened.
bool writeMultiLevelGCode(const std::vector<Cam3DLevel>& levels, const Cam3DParams& params, const std::string& path,
                          std::string* errorOut = nullptr);

} // namespace lcad
