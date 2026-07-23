#pragma once

#include "core/document/Document.h"

#include <TopoDS_Shape.hxx>

#include <vector>

namespace lcad {

// Standard orthographic/isometric view directions -- the eye direction and
// "up" convention for each is fixed and documented in TechDraw.cpp, since
// there's no way to visually confirm handedness/orientation choices on a
// machine with no real display (same disclosed constraint as Viewport3D.h).
// Bounding-box dimensions are the thing verified by this sprint's tests.
enum class ViewDirection { Front, Top, Right, Iso };

struct ProjectedEdge {
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
    bool hidden = false; // an edge HLR determined is occluded from this view
};

struct TechDrawView {
    std::vector<ProjectedEdge> edges;
};

// Projects shape's visible and hidden edges (via OCCT's HLRBRep_Algo, the
// same hidden-line-removal engine FreeCAD's own TechDraw workbench uses)
// into a flat 2D view. Curved edges are tessellated into many straight
// chords (see TechDraw.cpp's kCurveSegments) rather than exact conics --
// this codebase's 2D engine represents baked-in geometry as LineEntity
// segments here regardless, so a fine tessellation is the honest ceiling,
// not a full drafting-quality curve projector emitting real arcs/splines.
TechDrawView projectView(const TopoDS_Shape& shape, ViewDirection direction);

// Auxiliary view: an arbitrary projection direction, not restricted to the
// 4 fixed ViewDirection cases -- e.g. a view normal to an angled/chamfered
// face that no combination of Front/Top/Right/Iso shows true-shape. eye is
// the direction looking INTO the page (same convention as ViewDirection's
// own fixed eye directions); up is a reference direction (must not be
// parallel to eye) used to build the view's own local "up" axis -- it's
// projected perpendicular to eye first, exactly like a camera's up vector,
// so it doesn't need to be exactly perpendicular itself.
TechDrawView projectViewAux(const TopoDS_Shape& shape, double eyeX, double eyeY, double eyeZ, double upX, double upY,
                            double upZ);

// Section view: cuts shape with the plane through (originX,originY,originZ)
// with normal (normalX,normalY,normalZ) before projecting along direction --
// a real OCCT boolean cut against a generously-sized half-space box (see
// TechDraw.cpp), not a fake pass-through. The material on the POSITIVE
// side of the normal is removed; what's projected is what remains (the
// negative side) -- the usual "cutting plane looks toward the removed
// half" section convention. Disclosed simplification: the half-space box
// is sized from shape's own bounding box, so a cutting plane origin far
// outside that box isn't guaranteed to fully section it. Returns an empty
// view (no crash) if the cut fails or shape/normal is degenerate.
TechDrawView projectSectionView(const TopoDS_Shape& shape, ViewDirection direction, double originX, double originY,
                                double originZ, double normalX, double normalY, double normalZ);

// Bakes view into doc2d as LineEntity objects on a dedicated "TECHDRAW"
// layer (created if missing) -- visible edges stay Continuous (ByLayer),
// hidden edges get a per-entity LineType::Hidden override (reusing this
// codebase's existing linetype machinery rather than inventing a new "is
// this a hidden line" concept). Placed with an offset so multiple views
// can be laid out side by side on one sheet.
void insertViewIntoDocument(Document& doc2d, const TechDrawView& view, double offsetX, double offsetY);

struct AutoDimensionOptions {
    double offsetX = 0.0, offsetY = 0.0; // must match the same offsets insertViewIntoDocument used for this view
    double dimensionGap = 5.0;           // how far outside its own measured geometry each dimension line sits
    bool dimensionEachAxisAlignedEdge = false; // besides the two overall extents, also dimension every distinct visible horizontal/vertical edge
};

// Adds an overall-width DimensionEntity (horizontal, below the view) and
// an overall-height one (vertical, left of the view), both measured
// straight from view's own visible+hidden edge extents -- since
// ProjectedEdge coordinates for an orthographic view (Front/Top/Right)
// ARE the model's real dimensions along that view's own two axes (no
// separate 3D measurement needed), this is exact, not an estimate.
//
// Deliberately NOT supported for ViewDirection::Iso: a true isometric
// projection foreshortens every edge parallel to the 3 principal axes by
// the same constant factor (~0.8165), so lengths read directly off an
// Iso view's own plane are not the model's real lengths -- real drafting
// practice puts dimensions on orthographic views for exactly this
// reason, and this codebase doesn't attempt the separate isometric-scale
// correction that would be needed to do it correctly. Silently does
// nothing useful (no crash, just no dimensions with real meaning) if
// handed geometry projected under Iso.
//
// With dimensionEachAxisAlignedEdge, also adds one Linear dimension per
// distinct visible (non-hidden) horizontal or vertical edge, deduplicated
// by its own (axis, position, span) so a repeated/duplicate projected
// segment isn't dimensioned twice. Real, disclosed limitation: there's no
// dimension-line stacking/collision avoidance, so a busy view's per-edge
// dimensions may visually overlap each other or the overall ones -- only
// the measured values are guaranteed correct, not the layout.
void autoDimensionView(Document& doc2d, const TechDrawView& view, const AutoDimensionOptions& options = {});

} // namespace lcad
