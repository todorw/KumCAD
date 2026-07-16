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

    // Boolean-op operands: indices into the owning Document3D's feature
    // list. -1 means unset. Unused for primitive types.
    int inputA = -1;
    int inputB = -1;

    static bool isBoolean(FeatureType t) { return t == FeatureType::Union || t == FeatureType::Cut || t == FeatureType::Intersect; }
    bool isBoolean() const { return isBoolean(type); }
};

} // namespace lcad
