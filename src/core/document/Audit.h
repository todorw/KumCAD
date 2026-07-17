#pragma once

#include "core/Ids.h"

#include <string>
#include <vector>

namespace lcad {

class Document;

// AutoCAD's AUDIT command: scans the document for structural integrity
// issues and, if fix is true, repairs them. A real, but deliberately
// scoped-down, subset of what real AUDIT checks -- the most common
// corruption patterns a malformed or partially-written file can produce
// (dangling layer references, degenerate zero-size geometry), not
// exhaustive over every entity type. Real AUDIT's own dangling-block-
// reference and dimension-associativity checks are already handled
// elsewhere in this codebase (a block name DxfReader can't resolve is
// simply never referenced in the first place, and Document::
// reassociateDimensions already resolves-or-drops stale dimension
// anchors on load), so AUDIT doesn't re-check them here.
struct AuditIssue {
    EntityId entityId = 0;
    std::string description;
    bool fixed = false; // only meaningful when runAudit was called with fix=true
};

struct AuditResult {
    std::vector<AuditIssue> issues;
    int fixedCount = 0;
};

// fix=false (AUDIT's own default, matching real AutoCAD's "Fix any
// errors detected? [Yes/No]" prompt starting at No): reports issues
// without changing the document. fix=true: reassigns entities with a
// dangling layer reference to layer 0, and deletes degenerate geometry
// (zero-length lines, zero/negative-radius circles/arcs, polylines with
// fewer than 2 vertices).
AuditResult runAudit(Document& doc, bool fix);

} // namespace lcad
