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
    // One tool-radius-compensated contour per closed loop the slice at z
    // produced (see core/cam/Toolpath.h): toolpaths[0] is always the
    // largest loop (the outer boundary, compensated by params.side same
    // as before); toolpaths[1..] are every other loop at this level
    // (islands/pocket walls), each compensated Outside its own boundary
    // regardless of params.side, since an island is material to stay
    // clear of, not the profile to follow. This is still not full pocket-
    // clearing (nothing removes the material *between* the outer contour
    // and an island -- that needs an adaptive-clearing toolpath generator,
    // real additional scope) -- it's "the tool now also traces a contour
    // around every island instead of ignoring it entirely."
    std::vector<std::vector<Point2D>> toolpaths;
};

// "3D CAM" here means slice-based multi-level roughing, not full 3-axis
// surface finishing (a ball-nose tool following a solid's actual curved
// surfaces) -- that's a much deeper computational-geometry undertaking,
// flagged as one of the plan's riskiest items when Phase 3 was scoped.
// This slices shape with a horizontal plane at every Z from its top down
// to its bottom, params.stepDown apart (plus one final pass exactly at
// the bottom); see Cam3DLevel's own comment for how multiple loops per
// level (islands/pockets) are now handled.
std::vector<Cam3DLevel> sliceIntoLevels(const TopoDS_Shape& shape, const Cam3DParams& params);

// Writes every level as one G-code program, machining top to bottom, in
// the same plain-dialect conventions as core/cam/GCodeWriter.h (G21/G90
// header, retract to params.safeHeight between levels, M30 end). Returns
// false (with *errorOut set, if given) if levels is empty or the file
// can't be opened.
bool writeMultiLevelGCode(const std::vector<Cam3DLevel>& levels, const Cam3DParams& params, const std::string& path,
                          std::string* errorOut = nullptr);

} // namespace lcad
