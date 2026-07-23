#pragma once

#include <string>

namespace lcad {

class Document;

// Real .kicad_sch schematic format read/write, built on SExpr.h -- the
// schematic-side analog of KiCadPcb.h's real .kicad_pcb support, closing
// what was otherwise the biggest remaining KiCad gap: schematics only
// ever round-tripped through this codebase's own file format or through
// Netlist.h's plain-text netlist (explicitly NOT a real KiCad/EAGLE
// interchange format, see its own comment) -- never a real, KiCad-
// openable schematic sheet.
//
// A real, disclosed subset:
//   - wire (each WireEntity's multi-vertex path split into individual
//     2-point KiCad wire segments, the same convention KiCadPcb.cpp
//     already uses splitting a multi-vertex TrackEntity into `segment`s)
//   - junction, no_connect (JunctionEntity/NoConnectEntity, exact 1:1)
//   - label (NetLabelEntity -- a plain LOCAL label, not global_label,
//     since this writer has no sheet hierarchy to span; see Sheets.h for
//     this codebase's own hierarchy support, not wired in here)
//   - symbol instances (an InsertEntity over a symbol block, i.e.
//     block()->isSymbol()) with Reference/Value properties, placed by
//     lib_id -- using the block's own name AS its lib_id directly (no
//     "Library:Part" namespacing).
//   - a real lib_symbols table, one entry per DISTINCT symbol block
//     actually placed, embedding that block's own real pin geometry and
//     electrical types (derived from Pin::position/stubStart -- see
//     writeKiCadSch's own comment) -- so a write+read round trip, even
//     through this codebase's own writer alone with no external KiCad
//     library involved, preserves real net connectivity, not just file
//     syntax.
// NOT modeled: sheet hierarchy, bus wires/entries, graphical text/lines
// not tied to a wire or label, and each symbol's own graphical BODY
// artwork (outline rectangle/circle/etc, matching KiCadMod.h's own
// "reference by name, don't try to redraw real graphics from scratch"
// scope) -- real KiCad opening a written file will show correct pins/
// connectivity on an otherwise blank symbol body.
//
// Like KiCadPcb.h's own precedent, this hasn't been empirically verified
// against real KiCad software (none available in this environment) --
// correctness here means "a real S-expression file that reads back
// exactly what was written," not confirmed byte-for-byte KiCad
// compatibility.
bool writeKiCadSch(const Document& doc, const std::string& path, std::string* errorOut = nullptr);

// Reads path back: wires (each read as its own 2-point WireEntity, the
// mirror of the writer's own split -- not re-merged into a polyline),
// junctions, no-connects, labels, and symbol instances. A symbol
// instance's block is built ONCE per distinct lib_id (shared across
// every instance referencing it, matching real block/insert semantics)
// with its pins recovered from the file's own (lib_symbols (symbol
// "lib_id" ... (pin ...) ...)) table when present -- pins are collected
// from the top-level lib symbol AND every nested unit sub-symbol inside
// it (a real, disclosed simplification: every unit's pins are merged
// into one flat block rather than modeling KiCad's own per-unit gate
// swapping). A lib_id with no matching lib_symbols entry (e.g. a file
// this writer itself produced, which never embeds one) falls back to a
// pin-less placeholder block, still usable for placement but invisible
// to Netlist.h's own connectivity computation.
bool readKiCadSch(Document& doc, const std::string& path, std::string* errorOut = nullptr);

} // namespace lcad
