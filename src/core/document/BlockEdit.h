#pragma once

#include "core/document/Document.h"

#include <string>

namespace lcad {

// The real mechanism behind AutoCAD's BEDIT: a block's own entities are
// edited as an ordinary drawing (reusing every existing 2D editing command
// unchanged) in a document of their own, then written back into the SAME
// BlockDefinition on save.
//
// extractBlockForEditing builds that temporary document: every entity in
// blockName's own BlockDefinition::entities is CLONED with its ORIGINAL id
// preserved (clone() copies the id along with the geometry) -- unlike
// core/document/DocumentExtract.h's extractSubset, which deliberately
// assigns fresh ids for its own (WBLOCK/export) purpose. Preserving ids
// here matters because a block's dynamic parameters
// (DynamicVisibilityParameter::visibleIds and similar) reference specific
// child EntityIds, and applyBlockEdit below writes the edited set straight
// back into the same BlockDefinition, so those references need to still
// resolve afterward if the user didn't touch the entities they name. The
// returned document's own next-entity-id counter is bumped past the
// highest preserved id, so a NEW entity drawn during the session can't
// collide with one of them. source's own layers are copied too (matching
// ids, same "let existing references keep resolving" reasoning) so
// entities keep their real color/visibility while being edited; a nested
// INSERT inside the block still resolves correctly since it carries a raw
// pointer into source's own block table -- the caller is responsible for
// keeping source alive for as long as the returned document is used.
// Returns an otherwise-empty document if blockName doesn't resolve.
Document extractBlockForEditing(const Document& source, const std::string& blockName);

// The "Save Block Definition" step: replaces target's entire entities list
// with fresh clones of edited's own current entities. Not undoable,
// matching this codebase's existing precedent for block-metadata edits
// (PINADD, BlockParamCommand).
void applyBlockEdit(BlockDefinition& target, const Document& edited);

} // namespace lcad
