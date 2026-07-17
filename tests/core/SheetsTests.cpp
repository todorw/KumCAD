#include "core/document/Document.h"
#include "core/schematic/Sheets.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;

TEST_CASE("createSheet adds a new layer named SHEET:<name>", "[schematic][sheets]") {
    Document doc;
    const LayerId id = createSheet(doc, "Power");
    const Layer* layer = doc.findLayer(id);
    REQUIRE(layer != nullptr);
    REQUIRE(layer->name == "SHEET:Power");
}

TEST_CASE("listSheets reports every sheet layer, ignoring non-sheet layers", "[schematic][sheets]") {
    Document doc;
    doc.addLayer("0", Color{255, 255, 255});
    createSheet(doc, "Power");
    createSheet(doc, "Control");

    const std::vector<Sheet> sheets = listSheets(doc);
    REQUIRE(sheets.size() == 2);
    REQUIRE(std::any_of(sheets.begin(), sheets.end(), [](const Sheet& s) { return s.name == "Power"; }));
    REQUIRE(std::any_of(sheets.begin(), sheets.end(), [](const Sheet& s) { return s.name == "Control"; }));
}

TEST_CASE("goToSheet shows only the target sheet, hiding every other sheet layer", "[schematic][sheets]") {
    Document doc;
    const LayerId nonSheet = doc.addLayer("0", Color{255, 255, 255});
    const LayerId power = createSheet(doc, "Power");
    const LayerId control = createSheet(doc, "Control");

    REQUIRE(goToSheet(doc, "Control"));
    REQUIRE_FALSE(doc.findLayer(power)->visible);
    REQUIRE(doc.findLayer(control)->visible);
    REQUIRE(doc.findLayer(nonSheet)->visible); // non-sheet layers are untouched

    REQUIRE(goToSheet(doc, "Power"));
    REQUIRE(doc.findLayer(power)->visible);
    REQUIRE_FALSE(doc.findLayer(control)->visible);
}

TEST_CASE("goToSheet fails cleanly for an unknown sheet name", "[schematic][sheets]") {
    Document doc;
    createSheet(doc, "Power");
    REQUIRE_FALSE(goToSheet(doc, "NoSuchSheet"));
}
