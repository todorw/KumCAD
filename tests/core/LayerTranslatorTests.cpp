#include "core/document/Document.h"
#include "core/document/LayerTranslator.h"
#include "core/geometry/Line.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;
using Catch::Approx;

TEST_CASE("parseLayerTranslationFile reads mappings, properties, comments, and blank lines", "[laytrans]") {
    const std::string text =
        "# a comment line\n"
        "\n"
        "OldWalls=A-WALL\n"
        "OldDoors=A-DOOR,color=FF0000,linetype=DASHED,lineweight=0.5\n"
        "malformed line with no equals\n"
        "=NoOldName\n"
        "HasNewNameOnly=\n";

    const auto mappings = parseLayerTranslationFile(text);
    REQUIRE(mappings.size() == 2); // malformed/empty-name lines skipped

    REQUIRE(mappings[0].oldName == "OldWalls");
    REQUIRE(mappings[0].newName == "A-WALL");
    REQUIRE_FALSE(mappings[0].color.has_value());

    REQUIRE(mappings[1].oldName == "OldDoors");
    REQUIRE(mappings[1].newName == "A-DOOR");
    REQUIRE(mappings[1].color.has_value());
    REQUIRE(static_cast<int>(mappings[1].color->r) == 0xFF);
    REQUIRE(static_cast<int>(mappings[1].color->g) == 0x00);
    REQUIRE(mappings[1].linetype == LineType::Dashed);
    REQUIRE(mappings[1].lineweight.has_value());
    REQUIRE(*mappings[1].lineweight == Approx(0.5));
}

TEST_CASE("applyLayerTranslations renames a layer in place and applies property overrides", "[laytrans]") {
    Document doc;
    const LayerId oldLayer = doc.addLayer("OldWalls", Color{100, 100, 100});
    const EntityId id = doc.reserveEntityId();
    doc.addEntity(std::make_unique<LineEntity>(id, oldLayer, Point2D(0, 0), Point2D(1, 1)));

    LayerTranslation t;
    t.oldName = "OldWalls";
    t.newName = "A-WALL";
    t.color = Color{255, 0, 0};
    t.lineweight = 0.7;

    const auto result = applyLayerTranslations(doc, {t});
    REQUIRE(result.renamed == 1);
    REQUIRE(result.merged == 0);
    REQUIRE(result.notFound.empty());

    // Same layer id, new name/properties -- the entity's own layer
    // reference (by id) survives the rename untouched.
    const Layer* renamed = doc.findLayer(oldLayer);
    REQUIRE(renamed);
    REQUIRE(renamed->name == "A-WALL");
    REQUIRE(static_cast<int>(renamed->color.r) == 255);
    REQUIRE(renamed->lineweight == Approx(0.7));
    REQUIRE(doc.findEntity(id)->layer() == oldLayer);
}

TEST_CASE("applyLayerTranslations merges into an existing target layer, target properties win", "[laytrans]") {
    Document doc;
    const LayerId oldLayer = doc.addLayer("OldWalls", Color{100, 100, 100});
    const LayerId standardLayer = doc.addLayer("A-WALL", Color{0, 0, 255});
    doc.findLayer(standardLayer)->lineweight = 0.9;

    const EntityId id = doc.reserveEntityId();
    doc.addEntity(std::make_unique<LineEntity>(id, oldLayer, Point2D(0, 0), Point2D(1, 1)));

    LayerTranslation t;
    t.oldName = "OldWalls";
    t.newName = "A-WALL";
    t.color = Color{255, 0, 0}; // must be ignored -- target already exists

    const auto result = applyLayerTranslations(doc, {t});
    REQUIRE(result.merged == 1);
    REQUIRE(result.renamed == 0);

    REQUIRE(doc.findLayer(oldLayer) == nullptr); // source layer record deleted
    REQUIRE(doc.findEntity(id)->layer() == standardLayer); // entity moved onto the target
    REQUIRE(static_cast<int>(doc.findLayer(standardLayer)->color.b) == 255); // target's own color survives
    REQUIRE(doc.findLayer(standardLayer)->lineweight == Approx(0.9));
}

TEST_CASE("applyLayerTranslations records a missing oldName without erroring", "[laytrans]") {
    Document doc;
    LayerTranslation t;
    t.oldName = "DoesNotExist";
    t.newName = "A-WALL";

    const auto result = applyLayerTranslations(doc, {t});
    REQUIRE(result.renamed == 0);
    REQUIRE(result.merged == 0);
    REQUIRE(result.notFound == std::vector<std::string>{"DoesNotExist"});
}

TEST_CASE("applyLayerTranslations refuses to translate layer 0 away", "[laytrans]") {
    Document doc;
    LayerTranslation t;
    t.oldName = "0";
    t.newName = "A-WALL";

    const auto result = applyLayerTranslations(doc, {t});
    REQUIRE(result.renamed == 0);
    REQUIRE(result.merged == 0);
    REQUIRE(result.notFound == std::vector<std::string>{"0"});
    REQUIRE(doc.findLayer(0)->name == "0");
}

TEST_CASE("applyLayerTranslations processes multiple mappings in one call", "[laytrans]") {
    Document doc;
    doc.addLayer("Walls", Color{0, 0, 0});
    doc.addLayer("Doors", Color{0, 0, 0});

    std::vector<LayerTranslation> mappings;
    LayerTranslation a;
    a.oldName = "Walls";
    a.newName = "A-WALL";
    mappings.push_back(a);
    LayerTranslation b;
    b.oldName = "Doors";
    b.newName = "A-DOOR";
    mappings.push_back(b);

    const auto result = applyLayerTranslations(doc, mappings);
    REQUIRE(result.renamed == 2);

    bool foundWall = false, foundDoor = false;
    for (const Layer& l : doc.layers()) {
        if (l.name == "A-WALL") foundWall = true;
        if (l.name == "A-DOOR") foundDoor = true;
    }
    REQUIRE(foundWall);
    REQUIRE(foundDoor);
}
