#pragma once

#include <TopoDS_Shape.hxx>

#include <array>
#include <vector>

namespace lcad {

// A pipe run: a centerline path (>= 2 points) swept by a circular
// cross-section of outerRadius. Direction changes at interior points are
// auto-fitted with a sphere of the same outerRadius rather than a
// manufacturer-specific elbow fitting (long-radius 90-degree elbow, etc.)
// -- a real, disclosed simplification that's robust for *any* 3D turn
// angle (not just clean 45/90-degree ones), the same kind of blunt scope
// cut Sprint 3's Fillet/Chamfer made ("round every edge" instead of
// per-edge selection) and Assembly's mates made (closed-form instead of a
// general DOF solver). Isometric/orthographic piping drawings reuse
// TechDraw.h's projectView directly on the built shape -- no separate
// piping-specific projection code needed.
struct PipeRun {
    std::vector<std::array<double, 3>> path;
    double outerRadius = 25.0;
};

// Builds the fused solid: one cylinder per straight segment, one sphere
// per interior joint. Returns a null shape if path has fewer than 2
// points or outerRadius <= 0.
TopoDS_Shape buildPipeShape(const PipeRun& run);

} // namespace lcad
