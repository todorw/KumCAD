#pragma once

#include "core/geometry/Point2D.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace lcad {

class Document;
class TableEntity;

// A straight wall segment in plan, centered on (x1,y1)-(x2,y2), extruded
// from Z=0 up to height, thickness split evenly either side of that
// centerline -- covers the common straight-run case (an L/U/rectangular
// floor plan built from several walls), not curved walls or walls that
// change thickness partway, matching how far Phase 1's PCB/Electrical/P&ID
// tracks scoped their own schematic-engine reuse.
struct Wall {
    double x1 = 0.0, y1 = 0.0, x2 = 1000.0, y2 = 0.0;
    double height = 2700.0;
    double thickness = 200.0;
};

// A door (sillHeight == 0) or window (sillHeight > 0) cut into wallIndex,
// offsetAlongWall from the wall's own (x1,y1) start point.
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

struct BimModel {
    std::vector<Wall> walls;
    std::vector<Opening> openings;
    std::vector<Slab> slabs;
};

struct BimShapes {
    std::vector<TopoDS_Shape> wallShapes; // parallel to model.walls, with that wall's openings already cut
    std::vector<TopoDS_Shape> slabShapes; // parallel to model.slabs
};

// Builds every wall (openings assigned to it cut out) and every slab.
// A wall/slab that fails to build (degenerate dimensions) gets a null
// shape at its index rather than shrinking the vectors, so indices stay
// aligned with model.walls/model.slabs.
BimShapes buildBimShapes(const BimModel& model);

// Fuses every non-null shape in shapes into one compound, for feeding into
// TechDraw.h's projectView (a "plan" is just ViewDirection::Top of this,
// an "elevation" is Front/Right).
TopoDS_Shape combinedBimShape(const BimShapes& shapes);

// A deliberately minimal, DISCLOSED-NON-STANDARD "IFC-lite" export: valid
// ISO-10303-21 (STEP physical file) framing, but the entity types/
// attributes are this codebase's own simplified schema (wall centerline +
// height + thickness; opening's host wall + offset + dimensions; slab
// boundary + thickness + elevation) -- NOT real IFC4 entities. Real IFC
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

} // namespace lcad
