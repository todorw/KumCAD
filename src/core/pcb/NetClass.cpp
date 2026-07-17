#include "core/pcb/NetClass.h"

#include <algorithm>

namespace lcad {

const NetClass* findNetClass(const std::vector<NetClass>& netClasses, const std::string& netName) {
    if (netName.empty()) return nullptr;
    for (const NetClass& netClass : netClasses) {
        if (std::find(netClass.netNames.begin(), netClass.netNames.end(), netName) != netClass.netNames.end()) {
            return &netClass;
        }
    }
    return nullptr;
}

} // namespace lcad
