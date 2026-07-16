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
// into a flat 2D view. Curved edges are flattened to their endpoints only
// -- a real, disclosed simplification (a circle's silhouette becomes a
// single chord rather than a tessellated arc); good enough to prove the
// 3D-to-2D projection pipeline works, not a full drafting-quality curve
// projector.
TechDrawView projectView(const TopoDS_Shape& shape, ViewDirection direction);

// Bakes view into doc2d as LineEntity objects on a dedicated "TECHDRAW"
// layer (created if missing) -- visible edges stay Continuous (ByLayer),
// hidden edges get a per-entity LineType::Hidden override (reusing this
// codebase's existing linetype machinery rather than inventing a new "is
// this a hidden line" concept). Placed with an offset so multiple views
// can be laid out side by side on one sheet.
void insertViewIntoDocument(Document& doc2d, const TechDrawView& view, double offsetX, double offsetY);

} // namespace lcad
