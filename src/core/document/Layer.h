#pragma once

#include "core/Color.h"
#include "core/Ids.h"
#include "core/document/LineType.h"

#include <string>

namespace lcad {

struct Layer {
    LayerId id = 0;
    std::string name;
    Color color;
    LineType linetype = LineType::Continuous;
    bool visible = true;
    bool locked = false;
    double lineweight = 0.25; // mm; entities can override per-entity
};

} // namespace lcad
