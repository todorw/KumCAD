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
    std::vector<std::string> netNames; // which net names belong to this class
};

// Finds the class netName belongs to (by netNames membership), or
// nullptr if netName is empty, matches no class, or netClasses itself is
// empty -- callers fall back to their own single global rule in that
// case.
const NetClass* findNetClass(const std::vector<NetClass>& netClasses, const std::string& netName);

} // namespace lcad
