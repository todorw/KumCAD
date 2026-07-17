#pragma once

#include <TopoDS_Shape.hxx>

#include <array>
#include <optional>

namespace lcad {

struct PickRay {
    std::array<double, 3> origin{0.0, 0.0, 0.0};
    std::array<double, 3> direction{0.0, 0.0, 1.0}; // need not be normalized
};

struct FacePickResult {
    int faceIndex = -1; // 0-based index into TopExp::MapShapes(shape, TopAbs_FACE, ...)'s ordering
    std::array<double, 3> point{0.0, 0.0, 0.0};  // world hit point
    std::array<double, 3> normal{0.0, 0.0, 1.0}; // face's own outward normal at the hit point
    double distance = 0.0;                       // along the ray from its origin
};

struct EdgePickResult {
    int edgeIndex = -1; // 0-based index into TopExp::MapShapes(shape, TopAbs_EDGE, ...)'s ordering
    std::array<double, 3> point{0.0, 0.0, 0.0}; // nearest point on the edge to the ray
    double distance = 0.0;                      // that point's distance to the ray (perpendicular)
};

// Casts ray against every face of shape via real ray-surface intersection
// (OCCT's IntCurvesFace_Intersector, the same engine used for real
// mouse-picking in CAD viewers), returning the nearest hit (by distance
// along the ray) or nullopt if the ray misses entirely. This is pure
// geometry -- testable with an explicit ray and shape, no display needed,
// unlike the mouse-event wiring on top of it (Viewport3D's own
// unverified-in-this-environment territory).
std::optional<FacePickResult> pickFace(const TopoDS_Shape& shape, const PickRay& ray);

// Finds the edge whose closest point to ray's infinite line is nearest
// (and within tolerance) -- used for edge selection (Fillet/Chamfer),
// which needs an edge, not a face, and doesn't have a natural
// "intersection" the way a face pick does (a ray essentially never
// exactly meets a 1D edge), so this is nearest-point-to-line search
// instead of true intersection. Edges are tessellated (same technique as
// TechDraw.cpp/Cam3D.cpp's own curve sampling) rather than solved exactly.
std::optional<EdgePickResult> pickEdge(const TopoDS_Shape& shape, const PickRay& ray, double tolerance);

} // namespace lcad
