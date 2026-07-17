#pragma once

#include "core/geometry/Point2D.h"

#include <vector>

namespace lcad {

// A 2D sketch, solved by ConstraintSolver.h and extruded into a real 3D
// feature via Pad/Pocket/Revolve/Loft/Sweep. All constraint-solving math
// stays purely 2D (local x/y) regardless of where the sketch's plane sits
// in 3D -- see Sketch::placement below.
//
// Coincidence between geometry is structural, not a solved constraint: two
// lines sharing an endpoint literally share the same point index, the way
// most real sketchers actually represent it internally, rather than a
// numeric "distance == 0" equation the solver has to satisfy exactly.

// A minimal 3D point/vector -- this module is deliberately zero-OCCT
// (pure 2D math elsewhere in it), so this is a plain POD rather than
// pulling in gp_Pnt just for the plane's own placement.
struct Point3D {
    double x = 0.0, y = 0.0, z = 0.0;
};

// A sketch's own local XY plane's placement in 3D space: an origin plus a
// right-handed (xAxis, yAxis, normal) frame -- the same idea as OCCT's
// gp_Ax2, kept OCCT-free here since only core3d/SketchToFace.cpp actually
// needs to build real geometry from it. yAxis is always derived
// (normal x xAxis), never stored, so the frame can't drift out of
// orthogonality by editing one field and forgetting another.
//
// Defaults to the world XY plane at the origin -- exactly the implicit
// behavior every sketch had before this existed, so nothing changes
// unless a sketch's placement is set explicitly (via XY/XZ/YZ below, or
// by hand for a fully custom plane).
struct SketchPlane {
    Point3D origin{0.0, 0.0, 0.0};
    Point3D normal{0.0, 0.0, 1.0};
    Point3D xAxis{1.0, 0.0, 0.0};

    // FreeCAD's three datum base planes, each with an optional offset
    // along its own normal and an optional in-plane rotation (degrees,
    // about that normal) of the local X axis -- "attachment offset" and
    // "attachment angle" in FreeCAD's own terms, simplified to just these
    // two knobs rather than its full attachment-mode system.
    static SketchPlane XY(double offset = 0.0, double angleDegrees = 0.0);
    static SketchPlane XZ(double offset = 0.0, double angleDegrees = 0.0);
    static SketchPlane YZ(double offset = 0.0, double angleDegrees = 0.0);

    Point3D yAxis() const;
    // Maps a local 2D sketch point into this plane's 3D world position:
    // origin + xAxis*local.x + yAxis*local.y.
    Point3D toWorld(const Point2D& local) const;
};

struct SketchLine {
    int p1 = -1;
    int p2 = -1;
    bool construction = false; // construction geometry: solved like real geometry, not extruded
};

struct SketchCircle {
    int center = -1;
    double radius = 10.0;
    bool construction = false;
};

// An arc's start/end are real point indices (like SketchLine's p1/p2), so
// a line can structurally share an endpoint with an arc the same way it
// shares one with another line -- the whole point of this codebase's
// "coincidence is structural" design. radius is its own solver DOF (like
// SketchCircle's), and the solver keeps start/end pinned to that radius
// around center via an always-on internal consistency residual (see
// ConstraintSolver.cpp) rather than a constraint the user can remove --
// an arc that isn't actually circular isn't an arc. ccw says which of the
// two possible sweeps (short way vs. long way, in the counter-clockwise
// sense) between start and end is the intended one; there's no
// automatic "shortest arc" inference.
struct SketchArc {
    int center = -1;
    int start = -1;
    int end = -1;
    double radius = 10.0;
    bool ccw = true;
    bool construction = false;
};

enum class SketchConstraintType {
    Horizontal,   // geomA (line) is horizontal
    Vertical,     // geomA (line) is vertical
    Distance,     // |points[pointA] - points[pointB]| == value -- also how a sketch dimension is represented
    Parallel,     // geomA, geomB (lines) have parallel directions
    Perpendicular, // geomA, geomB (lines) have perpendicular directions
    Equal,        // geomA, geomB (lines) have equal length
    Tangent,      // geomA (line) is tangent to geomB (circle)
    Radius,       // geomA (circle) has radius == value -- how a circle gets dimensioned
    ArcRadius,    // geomA (arc) has radius == value -- how an arc gets dimensioned
    TangentCircleCircle, // geomA, geomB (circles) are externally tangent (distance(centers) == rA + rB) --
                         // internal tangency (one circle inside the other) isn't covered, a disclosed gap
    Angle,        // angle between geomA, geomB (lines) == value radians -- a general-purpose version of
                  // Perpendicular/Parallel, same cos-of-the-angle residual form so it shares their
                  // freedom from atan2's branch discontinuities
    PointOnLine,  // pointA lies on the infinite line through geomA (a line)'s two points -- for a point
                  // that needs to slide along a line without structurally sharing its endpoint index
    PointOnCircle, // pointA lies on geomA (a circle)
    Midpoint,     // pointA is the midpoint of geomA (a line) -- 2 scalar equations (x and y), unlike
                  // every other constraint type here
    Symmetric,    // pointA, pointB are mirror images of each other across geomA (a line, the symmetry
                  // axis) -- 2 scalar equations: their midpoint lies on the axis, and the segment
                  // between them is perpendicular to it
};

struct SketchConstraint {
    SketchConstraintType type = SketchConstraintType::Horizontal;
    int geomA = -1;
    int geomB = -1;
    int pointA = -1;
    int pointB = -1;
    double value = 0.0;
};

class Sketch {
public:
    int addPoint(Point2D p, bool fixed = false);
    int addLine(int p1, int p2, bool construction = false);
    int addCircle(int center, double radius, bool construction = false);
    int addArc(int center, int start, int end, double radius, bool ccw = true, bool construction = false);
    void addConstraint(SketchConstraint constraint);

    const std::vector<Point2D>& points() const { return m_points; }
    std::vector<Point2D>& points() { return m_points; }
    const std::vector<bool>& pointFixed() const { return m_fixed; }
    const std::vector<SketchLine>& lines() const { return m_lines; }
    std::vector<SketchLine>& lines() { return m_lines; }
    const std::vector<SketchCircle>& circles() const { return m_circles; }
    std::vector<SketchCircle>& circles() { return m_circles; }
    const std::vector<SketchArc>& arcs() const { return m_arcs; }
    std::vector<SketchArc>& arcs() { return m_arcs; }
    const std::vector<SketchConstraint>& constraints() const { return m_constraints; }
    std::vector<SketchConstraint>& constraints() { return m_constraints; }

    const SketchPlane& placement() const { return m_placement; }
    void setPlacement(SketchPlane plane) { m_placement = plane; }

private:
    std::vector<Point2D> m_points;
    std::vector<bool> m_fixed;
    std::vector<SketchLine> m_lines;
    std::vector<SketchCircle> m_circles;
    std::vector<SketchArc> m_arcs;
    std::vector<SketchConstraint> m_constraints;
    SketchPlane m_placement; // defaults to the world XY plane, see SketchPlane's own comment
};

} // namespace lcad
