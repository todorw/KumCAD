#include "core/document/DataExtraction.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Table.h"

#include <algorithm>
#include <set>

namespace lcad {

DataExtractionResult extractBlockData(const Document& doc, const std::vector<std::string>& blockNames,
                                      const std::vector<std::string>& attributeTags) {
    struct Match {
        std::string blockName;
        Point2D position;
        const InsertEntity* insert;
    };
    std::vector<Match> matches;

    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Insert) continue;
        const auto* insert = static_cast<const InsertEntity*>(e);
        if (!insert->block()) continue;
        const std::string& name = insert->block()->name;
        if (!blockNames.empty()) {
            if (std::find(blockNames.begin(), blockNames.end(), name) == blockNames.end()) continue;
        } else if (insert->attributes().empty()) {
            continue; // auto mode: only attributed instances are interesting
        }
        matches.push_back({name, insert->position(), insert});
    }

    DataExtractionResult result;
    if (!attributeTags.empty()) {
        result.attributeColumns = attributeTags;
    } else {
        std::set<std::string> tagSet;
        for (const Match& m : matches) {
            for (const auto& [tag, value] : m.insert->attributes()) tagSet.insert(tag);
        }
        result.attributeColumns.assign(tagSet.begin(), tagSet.end());
    }

    std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
        if (a.blockName != b.blockName) return a.blockName < b.blockName;
        if (a.position.x != b.position.x) return a.position.x < b.position.x;
        return a.position.y < b.position.y;
    });

    for (const Match& m : matches) {
        DataExtractionRow row;
        row.blockName = m.blockName;
        row.position = m.position;
        row.values.reserve(result.attributeColumns.size());
        for (const std::string& tag : result.attributeColumns) {
            const std::string* value = m.insert->attributeValue(tag);
            row.values.push_back(value ? *value : std::string());
        }
        result.rows.push_back(std::move(row));
    }
    return result;
}

TableEntity* buildDataExtractionTable(Document& doc2d, const DataExtractionResult& result, Point2D position) {
    const std::size_t colCount = 3 + result.attributeColumns.size(); // Block, X, Y, then attributes
    const std::size_t rowCount = result.rows.size() + 1;

    std::vector<double> rowHeights(rowCount, 4.0);
    std::vector<double> colWidths(colCount, 20.0);
    std::vector<std::string> cells(rowCount * colCount);

    cells[0] = "Block";
    cells[1] = "X";
    cells[2] = "Y";
    for (std::size_t c = 0; c < result.attributeColumns.size(); ++c) cells[3 + c] = result.attributeColumns[c];

    for (std::size_t i = 0; i < result.rows.size(); ++i) {
        const DataExtractionRow& row = result.rows[i];
        const std::size_t r = (i + 1) * colCount;
        cells[r + 0] = row.blockName;
        cells[r + 1] = std::to_string(row.position.x);
        cells[r + 2] = std::to_string(row.position.y);
        for (std::size_t c = 0; c < row.values.size(); ++c) cells[r + 3 + c] = row.values[c];
    }

    auto table = std::make_unique<TableEntity>(doc2d.reserveEntityId(), doc2d.currentLayer(), position, rowHeights,
                                                colWidths, cells);
    TableEntity* raw = table.get();
    doc2d.addEntity(std::move(table));
    return raw;
}

} // namespace lcad
