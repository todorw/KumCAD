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

struct BimModel {
    std::vector<Wall> walls;
    std::vector<Opening> openings;
    std::vector<Slab> slabs;
    std::vector<Column> columns;
    std::vector<Beam> beams;
    std::vector<Space> spaces;
};

struct BimShapes {
    std::vector<TopoDS_Shape> wallShapes;   // parallel to model.walls, with that wall's openings already cut
    std::vector<TopoDS_Shape> slabShapes;   // parallel to model.slabs
    std::vector<TopoDS_Shape> columnShapes; // parallel to model.columns
    std::vector<TopoDS_Shape> beamShapes;   // parallel to model.beams
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
