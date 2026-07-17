#pragma once

#include <string>
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
    Imported,      // a shape read from an external STEP/IGES file (see
                   // StepIges.h) or loaded back from a .kcad3d's embedded
                   // BRep blob (see Persistence3D.h). Has no parametric
                   // recipe of its own -- importIndex points into the owning
                   // Document3D's importedShapes(), and recompute just
                   // copies that shape verbatim rather than rebuilding it.
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

    // Which Document3D::sketches() entry to profile -- Pad/Revolve only.
    int sketchIndex = -1;

    // Repeat count (including the original) -- LinearPattern/PolarPattern only.
    int count = 1;

    // Which Document3D::importedShapes() entry this feature is -- Imported only.
    int importIndex = -1;

    // Specific edges to round/bevel -- Fillet/Chamfer only. Empty means
    // "every edge" (see FeatureType::Fillet's own comment).
    std::vector<int> edgeIndices;

    static bool isBoolean(FeatureType t) { return t == FeatureType::Union || t == FeatureType::Cut || t == FeatureType::Intersect; }
    bool isBoolean() const { return isBoolean(type); }
};

} // namespace lcad
