#pragma once

#include "core/Ids.h"
#include "core/schematic/Netlist.h"

#include <string>
#include <vector>

namespace lcad {

class Document;

// One ERC finding.
struct ErcIssue {
    enum class Severity { Warning, Error };
    Severity severity;
    std::string message;
    EntityId insertId = 0; // the symbol instance the issue is about, 0 if net-wide
};

// Runs ERC over nets (as returned by computeNets(doc)), plus document-wide
// (not net-scoped) checks. Net-scoped checks:
//  - a pin with no NoConnect marker sitting alone in its own net (nothing
//    wired to it) -- Warning, unless its electrical type is NotConnected.
//  - a pin whose electrical type is NotConnected but whose net has more
//    than one pin (it *is* wired to something) -- Warning.
//  - a net with more than one Output-type pin (driver conflict) -- Error.
//  - a net with a Power-type pin (a power INPUT -- an IC's VCC/GND, a
//    relay coil terminal, etc.) but no Output or PowerOutput pin
//    anywhere on it (nothing actually sourcing that power) -- Warning,
//    real KiCad's own "input power pin not driven" check, one of its
//    most common real findings (a supply pin left dangling). PowerOutput
//    is the distinct pin type a real power source (a battery/regulator
//    terminal) uses -- see PinElectricalType in Block.h.
//  - a pin-electrical-type conflict matrix approximating KiCad's own
//    default ERC severity table (not a byte-identical copy -- KiCad's own
//    matrix is user-configurable and has shifted across versions):
//      * more than one "hard driver" (Output/PowerOutput) tied together
//        that isn't already the plain Output-Output case above (e.g. an
//        Output tied to a PowerOutput, or two PowerOutputs shorted) --
//        Error.
//      * a Bidirectional or TriState pin sharing a net with a hard driver
//        -- Warning (potential bus contention; legitimate in a real
//        tri-stated bus, but worth a human's attention).
//      * an OpenCollector pin sharing a net with an Output pin -- Warning
//        (an OpenCollector wired *only* with other OpenCollector pins,
//        the real wired-OR/wired-AND pattern, is NOT flagged).
// Document-wide checks (independent of net topology):
//  - two or more symbol instances sharing the same non-empty REFDES
//    attribute -- Error, "duplicate reference designator".
//  - a symbol instance with a REFDES but no (or empty) FOOTPRINT
//    attribute -- Warning, "no footprint assigned" (mirrors KiCad's own
//    "unannotated footprint" ERC check).
std::vector<ErcIssue> runErc(const Document& doc, const std::vector<Net>& nets);

} // namespace lcad
