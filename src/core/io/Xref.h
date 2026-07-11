#pragma once

#include "core/document/Document.h"

#include <string>

namespace lcad {

// External references: a DXF (or DWG when LibreDWG is present) attached as a
// block definition whose entities are a cached snapshot of the file. The
// cache is written into the host DXF so drawings stay viewable when the
// referenced file is missing; Reload (and open, when reachable) refreshes it.

// Loads the file into a block definition named after the file stem (creating
// or refreshing it) and returns the definition, or nullptr with an error.
// The snapshot's entities land on xref-bound layers in the host document
// ("<xrefName>|<sourceLayerName>", matching AutoCAD's own naming), each
// carrying the source layer's color/linetype/lineweight, so the host can
// toggle individual xref layers instead of the reference always being one
// flat block.
const BlockDefinition* attachXref(Document& document, const std::string& path, std::string* errorOut = nullptr);

// Re-reads an attached xref's file into its definition (in place, so
// existing inserts keep pointing at it). False when the block isn't an xref
// or the file can't be read.
bool reloadXref(Document& document, const std::string& blockName, std::string* errorOut = nullptr);

// Refreshes every xref definition whose file is reachable, resolving
// relative paths against baseDir. Unreachable files keep their cached
// snapshot. Returns how many were refreshed.
int reloadAllXrefs(Document& document, const std::string& baseDir);

} // namespace lcad
