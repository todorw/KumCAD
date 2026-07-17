#pragma once

#include "core/Ids.h"

#include <string>
#include <vector>

namespace lcad {

class Document;

// One symbol instance's pin, identified by which INSERT it belongs to and
// the pin's number -- refdes is resolved via the INSERT's own ATTRIB
// attributes elsewhere, so the insert id is the join key here.
struct NetPin {
    EntityId insertId;
    std::string pinNumber;
};

// One electrically-connected group of pins, named either by an explicit
// NetLabelEntity touching it or an auto-generated "NetN".
struct Net {
    std::string name;
    std::vector<NetPin> pins;
};

// Computes electrical nets across doc's model space. A wire's own vertices
// always connect along its length; two different wires (or a wire and a
// pin) connect only where endpoints coincide, or at an explicit
// JunctionEntity for a T/cross connection -- matching KiCad's connectivity
// rule that crossing wires without a junction dot are NOT connected.
//
// Two NetLabelEntity instances with the SAME name connect their nets too,
// regardless of physical distance or which sheet (see core/schematic/
// Sheets.h) each sits on -- the real mechanism hierarchical/global labels
// use in KiCad/Eagle to link nets across sheets, not just a naming
// coincidence read back after the fact. A label only participates if it's
// actually touching something (a wire endpoint, a pin, or a junction-
// marked tap) -- one sitting on blank canvas with nothing under it doesn't
// connect anything, same as before.
std::vector<Net> computeNets(const Document& doc);

// A plain-text netlist, one NET block per net, listing each pin as
// "RefDes.PinNumber" (refdes resolved from the symbol instance's REFDES
// attribute, or "U<id>" if it has none). This is KumCAD's own simple
// interchange format, not a real KiCad/EAGLE-format netlist file --
// documented here rather than pretending compatibility with either.
std::string formatNetlist(const Document& doc, const std::vector<Net>& nets);

// Writes formatNetlist(doc, nets) to path. Returns false (with *errorOut
// set, if provided) on a file-open failure.
bool writeNetlist(const Document& doc, const std::vector<Net>& nets, const std::string& path,
                  std::string* errorOut = nullptr);

} // namespace lcad
