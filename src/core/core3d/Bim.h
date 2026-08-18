#pragma once

#include "core/geometry/Point2D.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace lcad {

class Document;
class TableEntity;

// A wall segment in plan, extruded from Z=0 up to height, thickness split
// evenly either side of its centerline -- still doesn't support walls
// that change thickness partway, matching how far Phase 1's PCB/
// Electrical/P&ID tracks scoped their own schematic-engine reuse.
//
// The default, common case (path left empty) is a single straight run
// centered on (x1,y1)-(x2,y2), unchanged from before. Setting path to 2+
// points instead gives the centerline a real multi-segment/curved shape
// -- built the same way real BIM tools chain wall runs and bay-window/
// atrium curves -- with x1/y1/x2/y2 then ignored for the wall's own
// shape (buildBimShapes) but still read by writeIfcLite as path.front()/
// path.back() for older-reader compatibility. bulges is parallel to
// path, reusing PolylineEntity's own DXF-bulge convention exactly
// (bulges[i] curves the segment path[i]->path[i+1]; 0 or a short vector
// means straight; the last entry is unused, same as an open polyline's
// own) -- so a straight-only multi-segment path just needs path itself,
// no bulges at all.
struct Wall {
    double x1 = 0.0, y1 = 0.0, x2 = 1000.0, y2 = 0.0;
    double height = 2700.0;
    double thickness = 200.0;
    std::vector<Point2D> path;
    std::vector<double> bulges;
};

// A door (sillHeight == 0) or window (sillHeight > 0) cut into wallIndex,
// offsetAlongWall measured as arc length from the wall's own start --
// (x1,y1) for a plain straight wall, or the first point of its path for
// a multi-segment/curved one, walking through path's straight and
// bulged-arc segments the same way its own shape is built.
struct Opening {
    int wallIndex = -1;
    double offsetAlongWall = 0.0;
    double width = 900.0;
    double height = 2100.0;
    double sillHeight = 0.0;
    bool isWindow = false;
};

// A flat slab: boundary is a simple (non-self-intersecting, no holes)
// closed polygon in plan, extruded by thickness starting at elevation.
struct Slab {
    std::vector<std::pair<double, double>> boundary;
    double thickness = 200.0;
    double elevation = 0.0;
};

// A vertical structural member centered at (x,y) in plan, running from
// baseElevation up to baseElevation+height -- rectangular (width x
// depth) by default, or circular (radius == width/2, depth unused) when
// round is set. Covers the common straight prismatic case, not tapered
// or non-prismatic columns.
struct Column {
    double x = 0.0, y = 0.0;
    double baseElevation = 0.0;
    double height = 3000.0;
    double width = 300.0, depth = 300.0; // depth unused when round
    bool round = false;
};

// A horizontal structural member running (x1,y1)-(x2,y2) in plan, its
// own rectangular cross-section (width horizontal across the beam,
// depth vertical) sitting with its bottom face at elevation -- the same
// centerline-and-local-frame convention Wall already uses, just placed
// at an arbitrary Z instead of always starting at 0.
struct Beam {
    double x1 = 0.0, y1 = 0.0, x2 = 1000.0, y2 = 0.0;
    double elevation = 2700.0;
    double width = 300.0, depth = 400.0;
};

// A room/space boundary in plan -- unlike Wall/Slab/Column/Beam, this is
// deliberately NEVER built into a 3D solid (see buildBimShapes): a space
// is fundamentally a labeled area for schedules (buildRoomScheduleTable
// below), the same real distinction IFC itself draws between IfcSpace
// and physical building elements.
struct Space {
    std::string name = "Room";
    std::vector<std::pair<double, double>> boundary;
};

// A roof over a footprint. hip selects a hip roof (every eave slopes up
// to a central ridge, collapsing to a point/pyramid for a small enough
// footprint) vs. a gable (only the two eaves running parallel to the
// ridge slope; the two perpendicular ends stay vertical gable walls).
// ridgeAlongX picks which footprint axis the ridge runs along (gable
// only -- a hip roof's ridge direction is implied by the footprint's own
// shape).
//
// A GABLE roof still requires an axis-aligned rectangle (exactly 4
// points) -- ridgeAlongX's "which two eaves are parallel to the ridge"
// classification only means something for a rectangle. A HIP roof
// generalizes to any CONVEX polygon (3+ points, any orientation): each
// eave contributes its own sloped-plane cut (buildRoofShape's own
// per-edge BRepAlgoAPI_Common loop), and intersecting one inward cut per
// edge is a well-defined operation for any convex shape, not just a
// rectangle -- a real triangular/pentagonal/hexagonal hip roof (a
// gazebo, a turret) now builds correctly. A NON-convex footprint (an
// L-shaped roof, needing an actual ridge/valley junction) stays out of
// scope: a reflex corner's inward cut can remove material a real roof
// wouldn't, which this simple per-edge-cut technique has no way to
// detect or correct for -- buildRoofShape rejects one outright rather
// than silently building a wrong shape.
struct Roof {
    std::vector<std::pair<double, double>> footprint; // expected: a convex polygon, CCW or CW
    double baseElevation = 3000.0;
    double pitchRadians = 0.4636; // ~26.57 deg, a common real default (a 2:1 slope)
    bool hip = false;
    bool ridgeAlongX = true;
};

// A straight-run stair: rise/run/width parameters generate stepCount real
// stepped solids (each tread+riser as one box), positioned starting at
// (x,y)+baseElevation and running along direction (dirX,dirY) (normalized
// internally), rising from baseElevation to baseElevation+totalRise.
// Winders and curved runs stay out of scope -- but a real switchback
// (U-shaped) or L-shaped stair is now just TWO Stair entries plus a
// Landing between them (baseElevation chained: flight 2's baseElevation ==
// flight 1's baseElevation+totalRise+landing.thickness), the same
// "compose from independent flat-list elements" convention every other
// BimModel element already uses rather than a dedicated multi-flight
// struct -- see Landing's own comment.
struct Stair {
    double x = 0.0, y = 0.0;
    double dirX = 1.0, dirY = 0.0;
    double width = 1000.0;
    double totalRise = 3000.0;
    int stepCount = 16;
    double treadDepth = 280.0;
    double baseElevation = 0.0;
};

// A flat rectangular platform, centered at (x,y) and rotated
// rotationDegrees around Z -- the piece a switchback/L-shaped stair
// chains between two Stair flights (see Stair's own comment), or an
// ordinary floor/porch landing on its own. Built the same simple
// box-at-elevation way Slab is, just with a fixed rectangular footprint
// (width x depth) instead of an arbitrary polygon boundary, since a
// landing is always a plain rectangle in practice.
struct Landing {
    double x = 0.0, y = 0.0;
    double width = 1000.0, depth = 1000.0;
    double thickness = 200.0;
    double baseElevation = 0.0;
    double rotationDegrees = 0.0;
};

struct BimModel {
    std::vector<Wall> walls;
    std::vector<Opening> openings;
    std::vector<Slab> slabs;
    std::vector<Column> columns;
    std::vector<Beam> beams;
    std::vector<Space> spaces;
    std::vector<Roof> roofs;
    std::vector<Stair> stairs;
    std::vector<Landing> landings;
};

struct BimShapes {
    std::vector<TopoDS_Shape> wallShapes;   // parallel to model.walls, with that wall's openings already cut
    std::vector<TopoDS_Shape> slabShapes;   // parallel to model.slabs
    std::vector<TopoDS_Shape> columnShapes; // parallel to model.columns
    std::vector<TopoDS_Shape> beamShapes;   // parallel to model.beams
    std::vector<TopoDS_Shape> roofShapes;   // parallel to model.roofs
    std::vector<TopoDS_Shape> stairShapes;  // parallel to model.stairs, each a compound of stepCount step solids
    std::vector<TopoDS_Shape> landingShapes; // parallel to model.landings
};

// Builds every wall (openings assigned to it cut out), slab, column, and
// beam -- model.spaces never gets a shape (see Space's own comment). An
// element that fails to build (degenerate dimensions) gets a null shape
// at its index rather than shrinking the vectors, so indices stay
// aligned with the model's own element vectors.
BimShapes buildBimShapes(const BimModel& model);

// Fuses every non-null shape in shapes into one compound, for feeding into
// TechDraw.h's projectView (a "plan" is just ViewDirection::Top of this,
// an "elevation" is Front/Right).
TopoDS_Shape combinedBimShape(const BimShapes& shapes);

// A deliberately minimal, DISCLOSED-NON-STANDARD "IFC-lite" export: valid
// ISO-10303-21 (STEP physical file) framing, but the entity types/
// attributes are this codebase's own simplified schema (wall centerline +
// height + thickness; opening's host wall + offset + dimensions; slab
// boundary + thickness + elevation; column position/base/height/section;
// beam centerline/elevation/section; space name + boundary) -- NOT real
// IFC4 entities. Real IFC
// geometry (IfcExtrudedAreaSolid over swept profile defs, a full spatial
// placement tree, property sets...) is much deeper than this sprint's
// scope, and no IFC library (IfcOpenShell) is available on this machine to
// vendor instead -- the same "write it from scratch, and disclose the
// limits honestly" call this codebase made for its sketch constraint
// solver instead of vendoring PlaneGCS. A real IFC viewer will NOT
// correctly open a file from this writer; treat it as this codebase's own
// round-trippable format that borrows IFC's file framing, not real
// interchange -- same spirit as the Gerber writer's "real subset, not
// full spec" disclosure.
bool writeIfcLite(const BimModel& model, const std::string& path);
bool readIfcLite(BimModel& model, const std::string& path);

// A door/window schedule (Type, Wall #, Width, Height, Sill) as a real
// TABLE entity in doc2d, one row per opening -- reuses TableEntity exactly
// as Phase 1's WireList/LineList reports did, rather than a new report
// concept.
TableEntity* buildOpeningScheduleTable(Document& doc2d, const BimModel& model, Point2D position);

// A room/space schedule (Name, Area, Perimeter) as a real TABLE entity,
// one row per model.spaces entry -- area via the shoelace formula,
// perimeter by summing consecutive boundary segment lengths (closing the
// loop back to the first point), same simple-polygon assumption Slab's
// own boundary already makes.
TableEntity* buildRoomScheduleTable(Document& doc2d, const BimModel& model, Point2D position);

} // namespace lcad
