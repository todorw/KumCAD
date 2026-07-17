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
// feature indices into a boolean; PAD3D extrudes an EXISTING sketch (by
// index -- there's no Lisp mini-language for describing a NEW sketch
// profile, a real, disclosed scope cut) into a Pad feature; VOLUME3D and
// BBOX3D query a feature's measured volume / (xmin ymin zmin xmax ymax
// zmax) bounding box; EXPORTSTEP3D writes the whole document's tip
// features to a STEP file (see StepIges.h). Every creation function
// returns nil instead of throwing when the feature ends up invalid
// (degenerate parameters, bad references) -- callable from a script
// without needing its own try/catch.
void registerLisp3DBindings(LispInterpreter& interp, Document3D& doc);

} // namespace lcad
