#pragma once

#include "core/core3d/Fingerprint.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace lcad {

// A parametric 3D feature. Which of p1-p4 mean what depends on type; unused
// fields are ignored. Position is a simple translation for every type;
// rotAxis/rotAngle (see Feature3D's own field comments) are an additional
// placement rotation, but only for the feature types that have their own
// independent orientation in space (primitives, Imported) -- Pad/Revolve
// already fully control their own orientation via dirX/Y/Z, and booleans/
// Fillet/Chamfer/patterns/Mirror derive their geometry from inputA/inputB
// so a standalone rotation on them wouldn't mean anything, matching how
// posX/Y/Z is already "unused fields are ignored" per type.
enum class FeatureType {
    Box,       // p1=dx, p2=dy, p3=dz
    Cylinder,  // p1=radius, p2=height
    Sphere,    // p1=radius
    Cone,      // p1=bottomRadius, p2=topRadius, p3=height
    Torus,     // p1=majorRadius, p2=minorRadius
    Wedge,     // p1=dx, p2=dy, p3=dz, p4=ltx (STEP right angular wedge taper)
    Union,     // inputA op inputB
    Cut,       // inputA op inputB
    Intersect, // inputA op inputB
    Pad,       // extrude sketchIndex's face by p1 along (dirX,dirY,dirZ). If
               // inputA is set: cutMode true = Pocket (inputA minus the
               // extrusion), cutMode false = auto-fuse (inputA plus the
               // extrusion) -- Pad/Pocket are the same feature type, not
               // separate ones, matching how a single click does either in
               // a real tool.
    Revolve,   // revolve sketchIndex's face by p1 degrees around the axis
               // through (posX,posY,posZ) with direction (dirX,dirY,dirZ).
               // inputA/cutMode work the same as Pad (Revolve/Groove).
    Fillet,    // rounds edgeIndices of inputA by radius p1 -- if
               // edgeIndices is empty, rounds EVERY edge instead (the
               // original, still-supported "blunt" mode). edgeIndices are
               // indices into TopExp::MapShapes(inputA's shape, TopAbs_EDGE,
               // ...)'s ordering, the same numbering Pick3D.h's pickEdge
               // returns -- so a real edge pick can drive this directly.
    Chamfer,   // bevels edgeIndices of inputA by distance p1 -- same
               // "empty means every edge" convention as Fillet.
    LinearPattern, // replicates inputA count times along (dirX,dirY,dirZ),
                   // spacing p1, fused into one shape (including the original)
    PolarPattern,  // replicates inputA count times around the axis through
                   // (posX,posY,posZ) with direction (dirX,dirY,dirZ), spread
                   // over p1 degrees total, fused into one shape
    Mirror,        // reflects inputA across the plane through (posX,posY,posZ)
                   // with normal (dirX,dirY,dirZ), fused with the original
                   // (a real Mirror feature keeps both; a plain "replace"
                   // copy would just discard the source, which is rarely
                   // what's wanted)
    Shell,         // hollows inputA to wall thickness p1, opening it up by
                   // removing faceIndices (a real Shell/Thickness feature
                   // needs at least one open face -- a fully sealed hollow
                   // shell isn't buildable/useful here, so an empty
                   // faceIndices is invalid, unlike Fillet/Chamfer's
                   // "empty means every edge" convention)
    Loft,          // builds a solid through 2+ sketch profiles
                   // (sketchIndices, each a Document3D::sketches() index),
                   // in listed order, via BRepOffsetAPI_ThruSections --
                   // a ruled (ThruSections' own default, no interior
                   // smoothing) surface between consecutive profiles'
                   // outer wires
    Sweep,         // sweeps sketchIndex's face profile along
                   // pathSketchIndex's own path via BRepOffsetAPI_MakePipe.
                   // Real, disclosed scope cut: the path must be exactly
                   // one straight SketchLine, not a multi-segment or
                   // curved wire -- MakePipe itself requires a G1-
                   // continuous spine (its own documented limitation),
                   // and a sharp-cornered polyline isn't one; a real
                   // multi-segment/curved sweep would need
                   // BRepOffsetAPI_MakePipeShell's own explicit corner-
                   // transition modes instead, which this doesn't attempt
    Draft,         // adds a p1-degree draft angle to inputA's faceIndices
                   // (only planar/cylindrical/conical faces can actually
                   // be drafted -- OCCT's own restriction, not this
                   // codebase's), pulled along (dirX,dirY,dirZ), relative
                   // to the neutral plane through (posX,posY,posZ) with
                   // that same direction as its own normal -- the common
                   // "draft measured from the pull direction" convention,
                   // via BRepOffsetAPI_DraftAngle
    Imported,      // a shape read from an external STEP/IGES file (see
                   // StepIges.h) or loaded back from a .kcad3d's embedded
                   // BRep blob (see Persistence3D.h). Has no parametric
                   // recipe of its own -- importIndex points into the owning
                   // Document3D's importedShapes(), and recompute just
                   // copies that shape verbatim rather than rebuilding it.
    Helix,         // a solid coil/spring/thread-like wire: p1=helix radius
                   // (distance from the axis), p2=pitch (axial rise per
                   // full turn), p3=height (total axial extent, so
                   // p3/p2 turns), p4=profile radius (thickness of the
                   // swept wire). Base point (posX,posY,posZ), axis
                   // direction (dirX,dirY,dirZ) -- same fields as
                   // Revolve's own axis. A real, disclosed helix (an
                   // actual helical spine via a line on a cylindrical
                   // surface, not a stack of rotated/translated rings),
                   // swept the same way Sweep already sweeps a profile
                   // along a path (BRepOffsetAPI_MakePipe) -- a helix is
                   // G1-continuous by construction, so it never hits
                   // Sweep's own disclosed sharp-corner limitation.
    Hole,          // drills into inputA at (posX,posY,posZ) along
                   // (dirX,dirY,dirZ): p1=diameter, p2=depth (a real,
                   // disclosed simplification: always a finite depth,
                   // not a true "through all" that adapts to the
                   // target's own extent -- give p2 comfortably larger
                   // than the target to emulate through-all). count
                   // REUSED as a 3-way hole-type selector (0=Simple,
                   // 1=Counterbore, 2=Countersink -- the same "reuse an
                   // existing generic field for a per-type discrete
                   // choice" convention cutMode already uses for Pad/
                   // Revolve): Counterbore's recess uses p3=counterbore
                   // diameter, p4=counterbore depth; Countersink's cone
                   // uses p3=countersink diameter, p4=full included
                   // angle in degrees (e.g. 82 or 90, standard drill
                   // countersink angles).
};

struct Feature3D {
    FeatureType type = FeatureType::Box;
    std::string name;

    double p1 = 10.0;
    double p2 = 10.0;
    double p3 = 10.0;
    double p4 = 0.0;

    double posX = 0.0;
    double posY = 0.0;
    double posZ = 0.0;

    // An additional placement rotation of rotAngle degrees around the axis
    // (rotAxisX,Y,Z) through (posX,posY,posZ) -- primitives (Box..Wedge)
    // and Imported only, applied after that type's own shape construction
    // (see recomputeOne). Defaults to "no rotation" (any axis, 0 degrees).
    double rotAxisX = 0.0;
    double rotAxisY = 0.0;
    double rotAxisZ = 1.0;
    double rotAngle = 0.0;

    // Direction/axis/plane-normal vector, meaning depends on type (see
    // FeatureType). Defaults to +Z, matching Pad's most common case
    // (extrude straight up from the sketch plane).
    double dirX = 0.0;
    double dirY = 0.0;
    double dirZ = 1.0;

    // Boolean-op operands, or the single solid a Fillet/Pattern/Mirror
    // applies to (inputA only), or the Pad/Revolve auto-combine target
    // (inputA only, see FeatureType::Pad): indices into the owning
    // Document3D's feature list. -1 means unset.
    int inputA = -1;
    int inputB = -1;
    bool cutMode = false; // Pad/Revolve only: true = Pocket/Groove (cut from inputA), false = fuse into inputA

    // Which Document3D::sketches() entry to profile -- Pad/Revolve/Sweep
    // only (for Sweep, the cross-section; the path is pathSketchIndex).
    int sketchIndex = -1;

    // Which Document3D::sketches() entry supplies the sweep path -- Sweep
    // only.
    int pathSketchIndex = -1;

    // Repeat count (including the original) -- LinearPattern/PolarPattern only.
    int count = 1;

    // Which Document3D::importedShapes() entry this feature is -- Imported only.
    int importIndex = -1;

    // Specific edges to round/bevel -- Fillet/Chamfer only. Empty means
    // "every edge" (see FeatureType::Fillet's own comment).
    std::vector<int> edgeIndices;

    // Which faces to remove (open up) -- Shell only. 0-based indices into
    // TopExp::MapShapes(inputA's shape, TopAbs_FACE, ...)'s ordering, the
    // same numbering Pick3D.h's pickFace returns -- so a real face pick
    // can drive this directly, same as edgeIndices does for Fillet/Chamfer.
    std::vector<int> faceIndices;

    // Geometric fingerprints (see core/core3d/TopoNaming.h), one per
    // edgeIndices/faceIndices entry in the SAME order, captured from
    // inputA's shape at the moment each edge/face was actually picked.
    // Recompute re-resolves each index against inputA's CURRENT shape
    // via its fingerprint before use -- the topological-naming
    // mitigation described in TopoNaming.h's own comment. Left empty
    // (the default, and always true for an older-format loaded file, or
    // for Fillet/Chamfer's "every edge" empty-edgeIndices mode) falls
    // back to trusting the raw indices directly, unchanged from before.
    std::vector<EdgeFingerprint> edgeFingerprints;
    std::vector<FaceFingerprint> faceFingerprints;

    // The profile sketches to loft through, in order -- Loft only (needs
    // 2+; sketchIndex above is unused for this type since it's not just
    // one profile).
    std::vector<int> sketchIndices;

    // Expression-driven parameters (FreeCAD's own expression engine,
    // simplified): keyed by the double field's own name ("p1", "posX",
    // "dirZ", ...; see core/core3d/Document3D.cpp's field table for the
    // exact set), each an arithmetic expression (core/util/Expr.h) that
    // can reference the owning Document3D's named variables. Evaluated at
    // the start of every recompute, OVERWRITING that field's stored value
    // -- so the field always shows the last successfully evaluated
    // result, and a field with no entry here behaves exactly as before
    // (a plain, directly-edited number). An expression that fails to
    // evaluate (bad syntax, unknown variable) silently leaves that
    // field's PREVIOUS value in place rather than invalidating the whole
    // feature -- a deliberately forgiving choice so one bad expression
    // doesn't collapse an entire downstream feature tree.
    std::unordered_map<std::string, std::string> expressions;

    static bool isBoolean(FeatureType t) { return t == FeatureType::Union || t == FeatureType::Cut || t == FeatureType::Intersect; }
    bool isBoolean() const { return isBoolean(type); }
};

} // namespace lcad
