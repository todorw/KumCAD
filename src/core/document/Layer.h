#pragma once

#include "core/Color.h"
#include "core/Ids.h"
#include "core/document/LineType.h"

#include <string>
#include <vector>

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

// One layer's saved properties within a LayerState snapshot.
struct LayerStateEntry {
    LayerId layerId = 0;
    bool visible = true;
    bool locked = false;
    Color color;
    LineType linetype = LineType::Continuous;
    double lineweight = 0.25;
};

// A named snapshot of every layer's visibility/lock/color/linetype/
// lineweight (AutoCAD's Layer States Manager / LAYERSTATE), restorable
// later. Entries reference layers by id, so restoring after a layer was
// deleted just skips that entry rather than erroring.
struct LayerState {
    std::string name;
    std::vector<LayerStateEntry> entries;
};

} // namespace lcad
