#pragma once

namespace lcad {

class LispInterpreter;
class Document3D;

// Registers a small set of 3D-modeling builtins into interp, closing
// KumCAD's "no scripting API" gap on the 3D side the same disclosed way
// the rest of this project's scripting story works: a real AutoLISP
// dialect (LispInterpreter.h), not Python (FreeCAD's own scripting
// language) -- consistent with how the 2D side already drives commands
// via (command ...), getvar/setvar, entget/ssget.
//
// Registered via LispInterpreter::registerBuiltin (see its own comment on
// why this lives here, in core3d, rather than making LispInterpreter.h
// itself depend on OCCT): BOX3D/CYLINDER3D/SPHERE3D create a primitive
// feature and return its index; UNION3D/CUT3D/INTERSECT3D combine two
// feature indices into a boolean; SKETCHNEW3D/SKETCHLINE3D/SKETCHCIRCLE3D/
// SKETCHARC3D build a NEW sketch profile procedurally (fixed-point
// geometry by raw coordinates -- no constraint solving, describing exact
// numbers directly, like ExternalGeometry.h's own projected points; a
// coordinate reused across calls reuses the SAME point rather than
// adding a structurally-disconnected duplicate, so a chain of
// SKETCHLINE3D calls sharing endpoints forms a real closed loop
// SketchToFace.cpp's own chaining can extrude, the same "coincidence by
// construction" the interactive editor's own point-snapping already
// gives for free), so PAD3D extruding a sketch no longer requires the
// interactive sketch editor first; VOLUME3D and BBOX3D query a feature's
// measured volume /
// (xmin ymin zmin xmax ymax zmax) bounding box; EXPORTSTEP3D writes the
// whole document's tip features to a STEP file (see StepIges.h). Every
// creation function returns nil instead of throwing when the feature ends
// up invalid (degenerate parameters, bad references) -- callable from a
// script without needing its own try/catch.
void registerLisp3DBindings(LispInterpreter& interp, Document3D& doc);

} // namespace lcad
