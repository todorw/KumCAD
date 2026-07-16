#pragma once

#include "core/sketch/SketchGeometry.h"

#include <TopoDS_Face.hxx>

#include <optional>

namespace lcad {

// Converts a solved sketch's non-construction lines/circles into a planar
// face: lines are chained by their shared point indices (coincidence is
// structural, see SketchGeometry.h) into closed loops, each circle is its
// own closed loop, the loop with the largest area becomes the outer
// boundary and every other loop becomes a hole. Returns nullopt if no
// closed loop exists at all (an open profile can't be padded/revolved).
//
// Simplification, disclosed: the line-chaining walk assumes each point in
// the profile has degree <= 2 (a simple polygon/profile, no branching) --
// the realistic shape for a pad/pocket sketch, not a general planar graph.
std::optional<TopoDS_Face> sketchToFace(const Sketch& sketch);

} // namespace lcad
