#include "core/document/Document.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Line.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("Document has a default layer", "[document]") {
    lcad::Document doc;
    REQUIRE(doc.layers().size() == 1);
    REQUIRE(doc.layers()[0].name == "0");
    REQUIRE(doc.currentLayer() == 0);
}

TEST_CASE("Document layer add/find", "[document]") {
    lcad::Document doc;
    const lcad::LayerId id = doc.addLayer("Walls", lcad::Color{255, 0, 0});
    REQUIRE(doc.layers().size() == 2);

    const lcad::Layer* layer = doc.findLayer(id);
    REQUIRE(layer != nullptr);
    REQUIRE(layer->name == "Walls");
    REQUIRE(doc.findLayer(999) == nullptr);
}

TEST_CASE("Document entity add/remove/find", "[document]") {
    lcad::Document doc;
    const lcad::EntityId id = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(1, 1)));

    REQUIRE(doc.entities().size() == 1);
    REQUIRE(doc.findEntity(id) != nullptr);

    auto removed = doc.removeEntity(id);
    REQUIRE(removed != nullptr);
    REQUIRE(doc.entities().empty());
    REQUIRE(doc.findEntity(id) == nullptr);
}

TEST_CASE("Associative dimensions follow the entities they measure", "[document][dimension]") {
    lcad::Document doc;

    auto line = std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), 0, lcad::Point2D(0, 0),
                                                   lcad::Point2D(10, 0));
    const lcad::EntityId lineId = line->id();
    doc.addEntity(std::move(line));

    auto dim = std::make_unique<lcad::DimensionEntity>(doc.reserveEntityId(), 0, lcad::Point2D(0, 0),
                                                       lcad::Point2D(10, 0), lcad::Point2D(5, 5), true);
    // A line's snap candidates: endpoints [start, end], then the midpoint.
    dim->setAnchor1(lcad::SnapRef{lineId, lcad::SnapKind::Endpoint, 0});
    dim->setAnchor2(lcad::SnapRef{lineId, lcad::SnapKind::Endpoint, 1});
    const lcad::EntityId dimId = dim->id();
    doc.addEntity(std::move(dim));

    // Stretch the line; the dimension's definition points follow.
    if (auto* e = doc.findEntity(lineId)) e->moveGripPoint(1, lcad::Point2D(20, 0));
    doc.reassociateDimensions();

    auto* dimE = static_cast<lcad::DimensionEntity*>(doc.findEntity(dimId));
    REQUIRE(dimE->point2().x == Approx(20.0));
    REQUIRE(dimE->geometry().value == Approx(20.0));

    // Deleting the measured entity makes the dimension non-associative but
    // keeps its last geometry.
    doc.removeEntity(lineId);
    doc.reassociateDimensions();
    REQUIRE_FALSE(dimE->anchor1().has_value());
    REQUIRE_FALSE(dimE->anchor2().has_value());
    REQUIRE(dimE->point2().x == Approx(20.0));
}

TEST_CASE("Active space routes new entities to layouts and back", "[document][layout]") {
    lcad::Document doc;
    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                                     lcad::Point2D(1, 0)));
    REQUIRE(doc.entities().size() == 1);
    REQUIRE(doc.paperEntities(0).empty());

    doc.setActiveSpace(0);
    const lcad::EntityId paperId = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(paperId, doc.currentLayer(), lcad::Point2D(5, 5),
                                                     lcad::Point2D(6, 5)));
    REQUIRE(doc.entities().size() == 1);      // model space untouched
    REQUIRE(doc.paperEntities(0).size() == 1);

    // Remove-then-re-add (the undo pattern) keeps the entity in its space.
    auto removed = doc.removeEntity(paperId);
    REQUIRE(removed);
    REQUIRE(doc.paperEntities(0).empty());
    doc.setActiveSpace(-1); // active space changed in between...
    doc.setActiveSpace(0);  // ...but re-adding in layout 0 restores it there
    doc.addEntity(std::move(removed));
    REQUIRE(doc.paperEntities(0).size() == 1);
    REQUIRE(doc.entities().size() == 1);
    doc.setActiveSpace(-1);
}

TEST_CASE("Named dim and text styles create, restore, and guard deletion", "[document][style]") {
    lcad::Document doc;
    REQUIRE(doc.currentDimStyleName() == "Standard");

    lcad::DimStyle big;
    big.textHeight = 10.0;
    doc.addOrUpdateDimStyle("Big", big);
    REQUIRE(doc.setCurrentDimStyle("Big"));
    REQUIRE(doc.dimStyle().textHeight == Catch::Approx(10.0));
    REQUIRE_FALSE(doc.setCurrentDimStyle("Missing"));
    REQUIRE(doc.setCurrentDimStyle("Standard"));
    REQUIRE(doc.dimStyle().textHeight == Catch::Approx(2.5));

    lcad::TextStyle narrow;
    narrow.name = "Narrow";
    narrow.widthFactor = 0.5;
    doc.addOrUpdateTextStyle(narrow);
    REQUIRE(doc.setCurrentTextStyle("Narrow"));
    REQUIRE(doc.findTextStyle("Narrow")->widthFactor == Catch::Approx(0.5));
    REQUIRE_FALSE(doc.setCurrentTextStyle("Missing"));
}

TEST_CASE("removeLayout drops paper entities and refuses the last layout", "[document][layout]") {
    lcad::Document doc;
    lcad::Layout extra;
    extra.name = "Extra";
    doc.layouts().push_back(extra);

    doc.setActiveSpace(1);
    const lcad::EntityId id = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id, doc.currentLayer(), lcad::Point2D(0, 0),
                                                     lcad::Point2D(1, 1)));
    doc.setActiveSpace(-1);

    REQUIRE(doc.removeLayout(1));
    REQUIRE(doc.findEntity(id) == nullptr); // its paper entities went with it
    REQUIRE(doc.layouts().size() == 1);
    REQUIRE_FALSE(doc.removeLayout(0)); // never delete the last layout
}

TEST_CASE("Groups select together and purge drops unused blocks and layers", "[document][group][purge]") {
    lcad::Document doc;
    const lcad::EntityId a = doc.reserveEntityId();
    const lcad::EntityId b = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(a, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(1, 0)));
    doc.addEntity(std::make_unique<lcad::LineEntity>(b, doc.currentLayer(), lcad::Point2D(0, 1), lcad::Point2D(1, 1)));

    doc.setGroup("pair", {a, b});
    const auto* members = doc.groupOf(a);
    REQUIRE(members);
    REQUIRE(members->size() == 2);
    REQUIRE(doc.removeGroup("pair"));
    REQUIRE(doc.groupOf(a) == nullptr);

    // An unused block and an empty layer purge away; used ones stay.
    std::vector<std::unique_ptr<lcad::Entity>> blockEnts;
    blockEnts.push_back(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), 0, lcad::Point2D(0, 0),
                                                           lcad::Point2D(1, 1)));
    doc.addBlock("unused", std::move(blockEnts));
    const lcad::LayerId emptyLayer = doc.addLayer("Empty", lcad::Color{1, 2, 3});
    (void)emptyLayer;

    const auto purged = doc.purge();
    REQUIRE(purged.blocks == 1);
    REQUIRE(purged.layers == 1);
    REQUIRE(doc.findBlock("unused") == nullptr);
}
