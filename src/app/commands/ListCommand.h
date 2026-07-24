#pragma once

#include "core/Ids.h"
#include "core/document/Document.h"

#include <QStringList>

// AutoCAD-style LIST: a real, type-specific property dump (geometry,
// not just "entity #N on layer X") for a set of entities -- exhaustive
// over EntityType (no default: case, so -Wswitch flags a future entity
// type that's never given here, the same mitigation this codebase's own
// bug-hunts have caught missing-case bugs with before). One entity's
// worth of lines per call to keep the caller (CommandDispatcher) free
// to interleave them with anything else it wants to print.
QStringList formatEntityList(const lcad::Document& document, lcad::EntityId id);
