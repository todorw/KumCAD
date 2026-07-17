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
    // Frozen (LAYFRZ/LAYTHW): hidden like visible=false, but a distinct
    // state exactly as in AutoCAD -- LAYON turns off-layers back on without
    // thawing frozen ones, and vice versa. (AutoCAD also skips regenerating
    // frozen layers' geometry; rendering here is cheap enough that frozen
    // and off simply both hide.) DXF: standard layer flag bit 1 (group 70).
    bool frozen = false;
    double lineweight = 0.25; // mm; entities can override per-entity
    std::string plotStyle; // names a PlotStyle in Document::plotStyles(); empty = plot as displayed
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
