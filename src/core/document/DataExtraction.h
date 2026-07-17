#pragma once

#include "core/geometry/Point2D.h"

#include <string>
#include <vector>

namespace lcad {

class Document;
class TableEntity;

// One placed INSERT instance's own row -- unlike Bom.h's generateBom(),
// which groups instances by (part, value), extractBlockData reports one row
// PER PLACED INSTANCE, matching AutoCAD's own DATAEXTRACTION/EATTEXT default
// (grouping there is an extra option layered on top, not attempted here).
struct DataExtractionRow {
    std::string blockName;
    Point2D position;
    std::vector<std::string> values; // parallel to DataExtractionResult::attributeColumns, "" if unset on this instance
};

struct DataExtractionResult {
    std::vector<std::string> attributeColumns; // attribute tags, in column order (after Block/X/Y)
    std::vector<DataExtractionRow> rows;
};

// Extracts one row per placed INSERT whose block name is in blockNames
// (every attributed INSERT if blockNames is empty), reporting blockName,
// insertion point, and the requested attribute values.
//
// attributeTags: empty means auto-discover -- the union of every distinct
// attribute tag actually set on a matching instance, alphabetically sorted
// for a stable column order. This mirrors real EATTEXT's own attribute
// picker, which starts from "every tag seen" rather than requiring the
// caller to already know the block's schema.
//
// Rows are sorted by (blockName, position.x, position.y) for a
// deterministic, reproducible report.
DataExtractionResult extractBlockData(const Document& doc, const std::vector<std::string>& blockNames = {},
                                      const std::vector<std::string>& attributeTags = {});

// A Block/X/Y/<attribute columns...> TABLE entity, one row per
// DataExtractionRow plus a header -- reuses TableEntity exactly as Bom.h's
// own buildBomTable does.
TableEntity* buildDataExtractionTable(Document& doc2d, const DataExtractionResult& result, Point2D position);

} // namespace lcad
