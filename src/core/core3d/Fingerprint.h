#pragma once

namespace lcad {

// Plain geometric fingerprints (see TopoNaming.h for how they're computed/
// resolved) -- split into their own OCCT-free header so Feature3D.h, which
// stores them per selected edge/face, doesn't need to pull in TopoDS_Shape
// and the rest of OCCT just to declare two small structs of doubles.

struct EdgeFingerprint {
    double midX = 0.0, midY = 0.0, midZ = 0.0;
    double length = 0.0;
};

struct FaceFingerprint {
    double centroidX = 0.0, centroidY = 0.0, centroidZ = 0.0;
    double area = 0.0;
};

} // namespace lcad
