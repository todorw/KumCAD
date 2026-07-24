#pragma once

#include "core/document/Document.h"

#include <vector>

namespace lcad {

// Builds a NEW, self-contained Document holding clones of exactly the
// entities in ids (source's own model-space entity ids; an id that
// doesn't resolve is silently skipped) -- the real mechanism behind
// AutoCAD's WBLOCK (extract a selection, or the whole drawing, into its
// own file). Every one of source's layers and block definitions is
// copied along too (not just the ones ids happens to touch -- the
// simplest way to guarantee every INSERT, including one nested inside
// another block, still resolves correctly in the new document, matching
// real WBLOCK's own "the extracted file is fully self-contained"
// promise; unused layers/blocks are harmless and PURGE-able afterward,
// same as a real AutoCAD wblock'd file often needs). Entity/layer ids
// in the result are freshly assigned, not preserved from source.
Document extractSubset(const Document& source, const std::vector<EntityId>& ids);

} // namespace lcad
