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

    bool isXref() const { return !xrefPath.empty(); }
    bool isDynamic() const {
        return dynamicParam || dynamicFlip || dynamicRotation || dynamicVisibility || dynamicArray || dynamicLookup;
    }
};

} // namespace lcad
