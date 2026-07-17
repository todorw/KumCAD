#pragma once

#include "core/geometry/Point2D.h"
#include "core/pcb/Stackup.h"

#include <string>
#include <vector>

namespace lcad {

class Document;

// One pin reference in an imported netlist: a component's reference
// designator + pin number, not a live EntityId -- see parseNetlist's own
// comment for why.
struct ImportedNetPin {
    std::string refDes;
    std::string pinNumber;
};

struct ImportedNet {
    std::string name;
    std::vector<ImportedNetPin> pins;
};

// Parses formatNetlist()'s output (core/schematic/Netlist.h) back into
// nets keyed by reference designator + pin number. KumCAD is
// single-document, so there's no live link between an open schematic and
// an open board -- netlist "import" here means: export from the schematic
// file, then read that text back in while a board document (with its own,
// different footprint INSERT entities) is open.
std::vector<ImportedNet> parseNetlist(const std::string& text);

struct RatsnestLine {
    Point2D a;
    Point2D b;
};

// Resolves each imported net's pins against doc's footprint INSERTs
// (matching REFDES attribute + pad number -- see BlockDefinition::pads),
// then returns the airwire segments still needed to connect them: pads
// already joined by existing Track/Via copper are collapsed into one
// cluster first (same endpoint-coincidence rule as schematic wires -- a
// Via is required at a trace's interior vertex for a real T-tap, matching
// JunctionEntity's role), then a minimum spanning tree connects whichever
// clusters remain separate. Pins that don't resolve to a placed pad are
// silently skipped.
//
// stackup defaults to empty, meaning every Track/Via is one shared copper
// plane regardless of layer (see CopperStackup's own comment in
// Stackup.h) -- pass a real stackup to make connectivity layer-aware.
std::vector<RatsnestLine> computeRatsnest(const Document& doc, const std::vector<ImportedNet>& nets,
                                          const CopperStackup& stackup = {});

} // namespace lcad
