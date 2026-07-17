#pragma once

#include "core/Ids.h"

#include <string>
#include <vector>

namespace lcad {

class Document;

// A schematic "sheet" is a layer by naming convention (any layer named
// "SHEET:<name>"), reusing Layer's existing name/visibility machinery
// rather than inventing new storage: sheet creation is layer creation,
// and switching the active sheet is just toggling layer visibility --
// both already fully supported, undoable (via the existing layer
// commands), and persisted (DXF layers round-trip today). Cross-sheet
// electrical connectivity is a separate concern, handled in
// core/schematic/Netlist.h's computeNets() via same-named NetLabel
// entities (the real hierarchical/global-label mechanism), not by
// anything here.
struct Sheet {
    LayerId layerId = 0;
    std::string name;
};

// Creates a new sheet layer named "SHEET:<name>" and returns its id.
LayerId createSheet(Document& doc, const std::string& name);

// Every sheet layer currently in doc, in layer order.
std::vector<Sheet> listSheets(const Document& doc);

// Shows only name's sheet layer, hiding every other sheet layer -- layers
// that aren't sheets (no "SHEET:" prefix) are left untouched, so common/
// sheet-independent content stays visible regardless of which sheet is
// active. Returns false (no change) if no sheet named name exists.
bool goToSheet(Document& doc, const std::string& name);

} // namespace lcad
