#pragma once

#include "core/geometry/Point2D.h"

#include <string>
#include <vector>

namespace lcad {

class Document;
class TableEntity;

// One grouped row of a Bill of Materials: every symbol INSERT sharing
// the same (part, value) groups into a single row -- KiCad's own BOM
// grouping rule (two 10k resistors group together; a 4.7k one gets its
// own row), not one row per placed component.
struct BomRow {
    std::string part;               // the symbol block's own name (e.g. "R")
    std::string value;               // the "VALUE" attribute if set on any grouped instance, else empty
    std::vector<std::string> refDes; // sorted plain string order (a real, disclosed simplification --
                                     // "R10" sorts before "R2", not true natural/numeric ordering)
    int quantity = 0;
    bool dnp = false; // true if every instance in this group carries a truthy DNP attribute
};

// Groups every schematic symbol INSERT (BlockDefinition::isSymbol(),
// same distinction Ratsnest.h's own isFootprint() makes on the PCB
// side) in doc by (block name, VALUE attribute), each becoming one
// BomRow, sorted by part then value for a stable, readable report.
// Components with no REFDES attribute at all are silently skipped (an
// unplaced/incomplete part isn't a real BOM line yet).
//
// KiCad-style DNP (Do-Not-Populate) variant support: an instance whose
// "DNP" attribute is truthy ("1"/"true"/"yes", case-insensitive) is
// excluded from the returned rows by default, matching KiCad's own
// default BOM behavior -- pass includeDnp=true to get a full report
// instead, where DNP instances group separately (never merged into a
// fitted group of the same part/value) and their row's dnp flag is set.
// Real, disclosed simplification: this is a single global DNP flag per
// instance, not KiCad's full per-design-variant "Fitted" matrix (which
// needs a variant/config concept this codebase doesn't have).
std::vector<BomRow> generateBom(const Document& doc, bool includeDnp = false);

// A Part/Value/Qty/DNP/RefDes(s) TABLE entity, one row per BomRow plus a
// header -- reuses TableEntity exactly as Phase 1's WireList/
// OpeningSchedule/RoomSchedule reports already do, rather than a new
// report concept.
TableEntity* buildBomTable(Document& doc2d, const std::vector<BomRow>& rows, Point2D position);

} // namespace lcad
