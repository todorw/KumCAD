#pragma once

#include "core/geometry/Entity.h"

#include <memory>
#include <string>
#include <vector>

namespace lcad {

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

    bool isXref() const { return !xrefPath.empty(); }
};

} // namespace lcad
