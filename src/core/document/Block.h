#pragma once

#include "core/geometry/Entity.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lcad {

// AutoCAD's dynamic block parameter kinds (each a simplified subset of a
// real Parameter+Action pair, merged into one object like AutoCAD's Block
// Editor keeps them conceptually paired). A block can carry at most one of
// each kind; InsertEntity holds the per-instance state each kind reads.

// Dragging the INSERT's grip at endPoint moves it along the
// basePoint->endPoint axis, and every child vertex inside the frame
// [frameMin, frameMax] (block-local coordinates, same as basePoint/endPoint)
// slides with it, the same windowed stretch as the STRETCH command (see
// ModifyOps::stretchedClone).
struct DynamicLinearParameter {
    Point2D basePoint;
    Point2D endPoint;
    Point2D frameMin;
    Point2D frameMax;
};

// A flip line through basePoint/endPoint; dragging the grip to the other
// side of the line mirrors every child entity across it (Entity::mirror).
struct DynamicFlipParameter {
    Point2D basePoint;
    Point2D endPoint;
};

// Dragging the grip around basePoint sets the instance's extra rotation,
// applied to every child entity about basePoint before the INSERT's own
// scale/rotation/translation.
struct DynamicRotationParameter {
    Point2D basePoint;
    double defaultRadius = 10.0; // grip distance from basePoint before any drag
};

// Named visibility states. visibleIds[state] lists the child entity ids
// shown in that state; a child id absent from every state's list is always
// shown (AutoCAD's default: unassigned objects appear in all states).
struct DynamicVisibilityParameter {
    std::vector<std::string> states;
    std::unordered_map<std::string, std::vector<EntityId>> visibleIds;
};

// A linear item array along direction (block-local, need not be unit
// length -- the stored length times spacing sets the per-item pitch);
// dragging the grip sets the instance's item count.
struct DynamicArrayParameter {
    Point2D basePoint;
    Point2D direction;
    double spacing = 10.0;
    int minCount = 1;
};

// A named list of presets, each an extra uniform scale factor applied to
// every child entity -- a simplified stand-in for AutoCAD's full lookup
// table (which can drive arbitrary other parameters per row).
struct DynamicLookupParameter {
    std::string name = "Lookup1";
    std::vector<std::pair<std::string, double>> presets;
};

// A schematic-symbol pin's electrical role, mirroring KiCad's pin electrical
// types -- used by ERC to flag illegal connections (e.g. two Output pins
// tied together) once the netlist exists.
enum class PinElectricalType {
    Input,
    Output,
    Bidirectional,
    TriState,
    Passive,
    Power, // a power INPUT (a load): needs an Output or PowerOutput pin driving its own net -- see Erc.h
    OpenCollector,
    NotConnected,
    // A power SOURCE (a battery/supply terminal, real KiCad's own distinct
    // "Power Output" pin type) -- added after the original 8 values so
    // existing DXF files' stored integer pin-type codes stay valid.
    PowerOutput,
};

// One connection point on a schematic symbol. position is the wire
// attachment point (block-local, like BlockDefinition's other geometry);
// stubStart is the far end of the drawn pin stub, so the stub segment runs
// stubStart -> position with the wire/net touching at position.
struct Pin {
    std::string name;
    std::string number;
    PinElectricalType electricalType = PinElectricalType::Passive;
    Point2D position;
    Point2D stubStart;
};

// A PCB footprint pad's shape.
enum class PadShape { Round, Rect, Oval };

// One copper connection point on a PCB footprint. number matches the
// schematic symbol pin number it corresponds to (by convention, like real
// footprints -- pad 1 is meant to receive the net wired to schematic pin 1).
// drillDiameter is 0 for a surface-mount pad, > 0 for plated through-hole --
// the same field also now doubles as the pad's own real per-side behavior
// (see Stackup.h/Board3D.h/GerberWriter.h's own comments): a through-hole
// pad's copper spans every stackup layer (an annular ring top to bottom
// around the plated hole), while a surface-mount pad's copper exists only
// on the ONE side its footprint is actually placed on (the owning
// InsertEntity's own layer) -- real KiCad's own SMD-vs-THT distinction,
// derived here rather than stored twice.
struct Pad {
    std::string number;
    PadShape shape = PadShape::Round;
    Point2D position;
    double width = 1.6;
    double height = 1.6;
    double drillDiameter = 0.0;
};

// A reusable group of entities (an AutoCAD block definition). Child geometry
// is stored relative to the block's base point, i.e. already translated so
// the base point is the local origin; an InsertEntity places, scales, and
// rotates it. Definitions are owned by the Document and live for its
// lifetime, so entities may hold plain pointers to them.
//
// xrefPath marks an external reference: the entities are a cached snapshot
// of the file at that path, refreshed by XREF Reload (and on open when the
// file is reachable). Empty for ordinary blocks.
struct BlockDefinition {
    std::string name;
    std::vector<std::unique_ptr<Entity>> entities;
    std::string xrefPath;
    std::optional<DynamicLinearParameter> dynamicParam;
    std::optional<DynamicFlipParameter> dynamicFlip;
    std::optional<DynamicRotationParameter> dynamicRotation;
    std::optional<DynamicVisibilityParameter> dynamicVisibility;
    std::optional<DynamicArrayParameter> dynamicArray;
    std::optional<DynamicLookupParameter> dynamicLookup;
    // Present iff this block is a schematic symbol (KiCad-style): an INSERT
    // of it becomes a symbol instance whose pins participate in netlist
    // computation (see core/schematic/Netlist.h). Empty for ordinary
    // (non-electrical) blocks.
    std::vector<Pin> pins;
    // Present iff this block is a PCB footprint: an INSERT of it becomes a
    // placed component whose pads participate in ratsnest computation (see
    // core/pcb/Ratsnest.h). A block is never both a symbol and a footprint.
    std::vector<Pad> pads;

    bool isXref() const { return !xrefPath.empty(); }
    bool isDynamic() const {
        return dynamicParam || dynamicFlip || dynamicRotation || dynamicVisibility || dynamicArray || dynamicLookup;
    }
    bool isSymbol() const { return !pins.empty(); }
    bool isFootprint() const { return !pads.empty(); }
};

} // namespace lcad
