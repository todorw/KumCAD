#pragma once

#include <string>

namespace lcad {

// A parametric 3D feature. Which of p1-p4 mean what depends on type; unused
// fields are ignored. Position is a simple translation only for now (no
// rotation) -- a real placement (rotation, sketch-plane orientation) is
// deeper 3D-sprint territory, not primitives-and-booleans foundations.
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
    Fillet,    // rounds every edge of inputA by radius p1 -- a real, if
               // blunt, simplification: no per-edge selection (needs 3D
               // edge-picking in a viewport this session can't verify --
               // see Viewport3D.h's own disclosure).
    Chamfer,   // bevels every edge of inputA by distance p1 -- same
               // all-edges simplification as Fillet.
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

    // Which Document3D::sketches() entry to profile -- Pad/Revolve only.
    int sketchIndex = -1;

    // Repeat count (including the original) -- LinearPattern/PolarPattern only.
    int count = 1;

    static bool isBoolean(FeatureType t) { return t == FeatureType::Union || t == FeatureType::Cut || t == FeatureType::Intersect; }
    bool isBoolean() const { return isBoolean(type); }
};

} // namespace lcad
