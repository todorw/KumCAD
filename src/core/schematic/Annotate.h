#pragma once

namespace lcad {

class Document;

// Real KiCad "Annotate Schematic": assigns sequential REFDES values (R1,
// R2, C1, C2, ...) to every placed symbol Insert that doesn't already
// have one, following the same real-and-already-shipped pattern
// core/pid/InstrumentTagging.h's own assignInstrumentTags uses (one
// hardcoded block name/prefix pair), generalized to every symbol via its
// own block name.
//
// Each symbol's prefix is its own block name with any trailing digits
// stripped -- e.g. "R" stays "R", "CONN2"/"CONN3"/"CONN4" (this
// codebase's own 2/3/4-pin connector symbols) all collapse to one shared
// "CONN" pool, matching how connectors of different pin counts still get
// numbered in one real-world J1, J2, J3... sequence. A real, disclosed
// simplification: there's no separate stored "reference prefix" field on
// BlockDefinition (unlike real KiCad's own per-symbol Reference field),
// so a symbol whose own name doesn't already look like a real prefix
// (e.g. "Q_NPN"/"Q_PNP", numbered as two independent sequences instead
// of one shared "Q" pool) annotates a little differently than real KiCad
// would -- not wrong, just not unified the way a curated Reference field
// would be.
//
// Each prefix's counter continues from the highest number ALREADY used
// with that prefix (so re-running this on a partially-annotated
// schematic never reassigns or collides with a REFDES a user set by
// hand), and an instance that already has any REFDES value is left
// untouched -- idempotent, the same guarantee assignInstrumentTags
// already gives. GND/VCC (this codebase's own power-flag symbols) are
// skipped entirely -- they're not discrete components with a reference
// designator in real schematic convention.
//
// Returns the number of instances actually labeled by this call.
int annotateSchematic(Document& doc);

} // namespace lcad
