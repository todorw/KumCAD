#include "core/document/DataExtraction.h"
#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Table.h"
#include "core/io/Csv.h"
#include "core/schematic/SymbolLibrary.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace lcad;

namespace {

InsertEntity* placeR(Document& doc, const Point2D& pos, const std::string& refDes, const std::string& value) {
    const BlockDefinition* block = doc.findBlock("R");
    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), block, pos);
    insert->setAttribute("REFDES", refDes);
    insert->setAttribute("VALUE", value);
    InsertEntity* raw = insert.get();
    doc.addEntity(std::move(insert));
    return raw;
}

} // namespace

TEST_CASE("extractBlockData reports one row per placed instance, not grouped like a BOM", "[document][dataextraction]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeR(doc, Point2D(0, 0), "R1", "10k");
    placeR(doc, Point2D(10, 0), "R2", "10k"); // same value as R1 -- would group in a BOM, must NOT here

    const DataExtractionResult result = extractBlockData(doc);
    REQUIRE(result.rows.size() == 2); // one row per instance, no grouping
}

TEST_CASE("extractBlockData auto-discovers attribute columns as a sorted union", "[document][dataextraction]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeR(doc, Point2D(0, 0), "R1", "10k");

    const DataExtractionResult result = extractBlockData(doc);
    REQUIRE(result.attributeColumns == std::vector<std::string>{"REFDES", "VALUE"});
}

TEST_CASE("extractBlockData filters by block name and skips unattributed instances by default",
         "[document][dataextraction]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeR(doc, Point2D(0, 0), "R1", "10k");
    const BlockDefinition* capBlock = doc.findBlock("C");
    REQUIRE(capBlock != nullptr);
    auto plainCap = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), capBlock, Point2D(5, 5));
    doc.addEntity(std::move(plainCap)); // no attributes set at all

    const DataExtractionResult all = extractBlockData(doc);
    REQUIRE(all.rows.size() == 1); // the unattributed capacitor is skipped in auto mode

    const DataExtractionResult filtered = extractBlockData(doc, {"C"});
    REQUIRE(filtered.rows.size() == 1); // explicit block-name filter includes it regardless of attributes
    REQUIRE(filtered.rows[0].blockName == "C");
}

TEST_CASE("extractBlockData rows carry position and the requested attribute values in column order",
         "[document][dataextraction]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeR(doc, Point2D(3.0, 4.0), "R1", "4.7k");

    const DataExtractionResult result = extractBlockData(doc, {}, {"VALUE", "REFDES"});
    REQUIRE(result.attributeColumns == std::vector<std::string>{"VALUE", "REFDES"});
    REQUIRE(result.rows.size() == 1);
    REQUIRE(result.rows[0].blockName == "R");
    REQUIRE(result.rows[0].position.x == 3.0);
    REQUIRE(result.rows[0].position.y == 4.0);
    REQUIRE(result.rows[0].values == std::vector<std::string>{"4.7k", "R1"});
}

TEST_CASE("extractBlockData rows are sorted by (blockName, x, y) for a reproducible report",
         "[document][dataextraction]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeR(doc, Point2D(10, 0), "R2", "10k");
    placeR(doc, Point2D(0, 0), "R1", "10k");

    const DataExtractionResult result = extractBlockData(doc);
    REQUIRE(result.rows.size() == 2);
    REQUIRE(result.rows[0].position.x == 0.0); // R1 at x=0 sorts before R2 at x=10
    REQUIRE(result.rows[1].position.x == 10.0);
}

TEST_CASE("buildDataExtractionTable produces a Block/X/Y/<attrs> header plus one row per instance",
         "[document][dataextraction]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeR(doc, Point2D(1.0, 2.0), "R1", "1k");

    const DataExtractionResult result = extractBlockData(doc);
    TableEntity* table = buildDataExtractionTable(doc, result, Point2D(0, 0));
    REQUIRE(table != nullptr);
    REQUIRE(table->rows() == 2); // header + 1 instance
    REQUIRE(table->cols() == 5); // Block, X, Y, REFDES, VALUE
    REQUIRE(table->cellText(0, 0) == "Block");
    REQUIRE(table->cellText(1, 0) == "R");
}

TEST_CASE("writeCsv round-trips through readCsv", "[io][csv][dataextraction]") {
    Document doc;
    registerBuiltinSymbols(doc);
    placeR(doc, Point2D(1.0, 2.0), "R1", "value, with a comma");

    const DataExtractionResult result = extractBlockData(doc);
    std::vector<std::vector<std::string>> rows;
    rows.push_back({"Block", "X", "Y"});
    rows.back().insert(rows.back().end(), result.attributeColumns.begin(), result.attributeColumns.end());
    for (const DataExtractionRow& row : result.rows) {
        std::vector<std::string> csvRow{row.blockName, std::to_string(row.position.x), std::to_string(row.position.y)};
        csvRow.insert(csvRow.end(), row.values.begin(), row.values.end());
        rows.push_back(csvRow);
    }

    const std::string path = "/tmp/kumcad_data_extraction_test.csv";
    std::string error;
    REQUIRE(writeCsv(path, rows, &error));

    const auto readBack = readCsv(path);
    REQUIRE(readBack.has_value());
    REQUIRE(*readBack == rows);
    std::remove(path.c_str());
}
