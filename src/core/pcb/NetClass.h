#pragma once

#include <string>
#include <vector>

namespace lcad {

// A named group of nets sharing routing/DRC rules (KiCad's own "Net
// Class" concept) -- e.g. a "Power" class with a wider trackWidth than
// "Default", or a "HighVoltage" class with extra clearance. Every
// consumer here (runDrc, autoroute) defaults to an empty net class list,
// which means "one shared global rule set for everything," the exact
// pre-existing behavior -- passing real classes is what turns on
// per-net-class rules.
struct NetClass {
    std::string name = "Default";
    double clearance = 0.2;
    double trackWidth = 0.25;
    // Real KiCad net classes also carry via size and diff-pair geometry
    // on top of clearance/trackWidth -- wired into autoroute() (a
    // connection needing a layer-switch via on this class uses these
    // instead of AutorouteParams' own single global via size, the same
    // per-class-overrides-global pattern trackWidth/clearance already
    // follow there) and into DiffPair.h's own NetClass-aware
    // routeDiffPair overload.
    double viaDiameter = 0.6;
    double viaDrillDiameter = 0.3;
    double diffPairGap = 0.2;
    double diffPairWidth = 0.25;
    std::vector<std::string> netNames; // which net names belong to this class
};

// Finds the class netName belongs to (by netNames membership), or
// nullptr if netName is empty, matches no class, or netClasses itself is
// empty -- callers fall back to their own single global rule in that
// case.
const NetClass* findNetClass(const std::vector<NetClass>& netClasses, const std::string& netName);

} // namespace lcad
