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

TEST_CASE("Layer states capture, save, apply, and delete", "[document][layerstate]") {
    lcad::Document doc;
    const lcad::LayerId wallsLayer = doc.addLayer("Walls", lcad::Color{255, 0, 0});
    REQUIRE(doc.layerStates().empty());

    doc.saveLayerState("Base");
    REQUIRE(doc.layerStates().size() == 1);
    REQUIRE(doc.layerStates()[0].name == "Base");
    REQUIRE(doc.layerStates()[0].entries.size() == doc.layers().size());

    // Change the layer, then apply the saved state back over it.
    lcad::Layer* walls = doc.findLayer(wallsLayer);
    walls->visible = false;
    walls->locked = true;
    doc.applyLayerState(doc.layerStates()[0]);
    REQUIRE(doc.findLayer(wallsLayer)->visible);
    REQUIRE_FALSE(doc.findLayer(wallsLayer)->locked);

    // Saving under the same name overwrites rather than duplicating.
    doc.saveLayerState("Base");
    REQUIRE(doc.layerStates().size() == 1);

    REQUIRE(doc.deleteLayerState("Base"));
    REQUIRE(doc.layerStates().empty());
    REQUIRE_FALSE(doc.deleteLayerState("Base")); // already gone
}

TEST_CASE("Applying a layer state skips entries for since-deleted layers", "[document][layerstate]") {
    lcad::Document doc;
    const lcad::LayerId wallsLayer = doc.addLayer("Walls", lcad::Color{255, 0, 0});
    doc.saveLayerState("Base");
    REQUIRE(doc.purge().layers == 1); // "Walls" is empty and unused, so purge drops it

    // Applying the state must not crash or resurrect the deleted layer.
    doc.applyLayerState(doc.layerStates()[0]);
    REQUIRE(doc.findLayer(wallsLayer) == nullptr);
    REQUIRE(doc.layers().size() == 1);
}

TEST_CASE("Plot styles save, overwrite, and delete", "[document][plotstyle]") {
    lcad::Document doc;
    REQUIRE(doc.plotStyles().empty());

    lcad::PlotStyle style;
    style.name = "Thin Black";
    style.color = lcad::Color{0, 0, 0};
    style.lineweight = 0.13;
    doc.savePlotStyle(style);
    REQUIRE(doc.plotStyles().size() == 1);
    REQUIRE(doc.findPlotStyle("Thin Black") != nullptr);
    REQUIRE(doc.findPlotStyle("Thin Black")->lineweight == Approx(0.13));
    REQUIRE(doc.findPlotStyle("Nonexistent") == nullptr);

    // Saving under the same name overwrites rather than duplicating.
    style.lineweight = 0.25;
    doc.savePlotStyle(style);
    REQUIRE(doc.plotStyles().size() == 1);
    REQUIRE(doc.findPlotStyle("Thin Black")->lineweight == Approx(0.25));

    REQUIRE(doc.deletePlotStyle("Thin Black"));
    REQUIRE(doc.plotStyles().empty());
    REQUIRE_FALSE(doc.deletePlotStyle("Thin Black"));
}

TEST_CASE("plotAppearance layers layer, entity override, then plot style", "[document][plotstyle]") {
    lcad::Document doc;
    const lcad::LayerId wallsLayer = doc.addLayer("Walls", lcad::Color{200, 50, 50});
    doc.findLayer(wallsLayer)->lineweight = 0.35;
    doc.findLayer(wallsLayer)->linetype = lcad::LineType::Dashed;

    const lcad::EntityId id = doc.reserveEntityId();
    auto line =
        std::make_unique<lcad::LineEntity>(id, wallsLayer, lcad::Point2D(0, 0), lcad::Point2D(1, 1));
    doc.addEntity(std::move(line));

    // No overrides, no plot style: appearance is just the layer's.
    lcad::PlotAppearance appearance = doc.plotAppearance(*doc.findEntity(id));
    REQUIRE(appearance.color.r == 200);
    REQUIRE(appearance.lineweight == Approx(0.35));
    REQUIRE(appearance.linetype == lcad::LineType::Dashed);

    // An entity color override beats the layer.
    doc.findEntity(id)->setColorOverride(lcad::Color{10, 20, 30});
    appearance = doc.plotAppearance(*doc.findEntity(id));
    REQUIRE(appearance.color.r == 10);
    REQUIRE(appearance.lineweight == Approx(0.35)); // lineweight still from the layer

    // A plot style assigned to the layer overrides even the entity's own color.
    lcad::PlotStyle style;
    style.name = "Print Black";
    style.color = lcad::Color{0, 0, 0};
    style.lineweight = 0.05;
    doc.savePlotStyle(style);
    doc.findLayer(wallsLayer)->plotStyle = "Print Black";

    appearance = doc.plotAppearance(*doc.findEntity(id));
    REQUIRE(appearance.color.r == 0);
    REQUIRE(appearance.color.g == 0);
    REQUIRE(appearance.lineweight == Approx(0.05));
    REQUIRE(appearance.linetype == lcad::LineType::Dashed); // style doesn't override linetype: falls through
}

TEST_CASE("applyScreening blends toward paper white by ink percentage", "[document][plotstyle]") {
    const lcad::Color black{0, 0, 0};
    const lcad::Color full = lcad::applyScreening(black, 100.0);
    REQUIRE(static_cast<int>(full.r) == 0);
    const lcad::Color half = lcad::applyScreening(black, 50.0);
    REQUIRE(static_cast<int>(half.r) == 128);
    REQUIRE(static_cast<int>(half.g) == 128);
    const lcad::Color none = lcad::applyScreening(black, 0.0);
    REQUIRE(static_cast<int>(none.r) == 255);
    // Screening a non-gray color keeps its hue direction: 50% of pure red
    // moves g/b halfway to white but leaves r at full.
    const lcad::Color red50 = lcad::applyScreening(lcad::Color{255, 0, 0}, 50.0);
    REQUIRE(static_cast<int>(red50.r) == 255);
    REQUIRE(static_cast<int>(red50.g) == 128);
}

TEST_CASE("Color-dependent (CTB) mode picks the pen from the displayed color's ACI", "[document][plotstyle]") {
    lcad::Document doc;
    const lcad::LayerId redLayer = doc.addLayer("Red", lcad::Color{255, 0, 0}); // ACI 1

    const lcad::EntityId id = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id, redLayer, lcad::Point2D(0, 0), lcad::Point2D(1, 1)));

    // A named style is assigned too -- CTB mode must ignore it entirely.
    lcad::PlotStyle named;
    named.name = "ShouldBeIgnored";
    named.color = lcad::Color{1, 2, 3};
    doc.savePlotStyle(named);
    doc.findLayer(redLayer)->plotStyle = "ShouldBeIgnored";

    lcad::CtbEntry pen;
    pen.aci = 1; // red
    pen.color = lcad::Color{0, 0, 0};
    pen.lineweight = 0.7;
    doc.saveCtbEntry(pen);

    // Named mode (default): the named style applies, the CTB row doesn't.
    lcad::PlotAppearance appearance = doc.plotAppearance(*doc.findEntity(id));
    REQUIRE(static_cast<int>(appearance.color.r) == 1);

    doc.setPlotStyleMode(lcad::PlotStyleMode::ColorDependent);
    appearance = doc.plotAppearance(*doc.findEntity(id));
    REQUIRE(static_cast<int>(appearance.color.r) == 0); // CTB pen color, not the named style's
    REQUIRE(appearance.lineweight == Approx(0.7));

    // An entity drawn in a color with no CTB row plots as displayed.
    const lcad::EntityId greenId = doc.reserveEntityId();
    auto green = std::make_unique<lcad::LineEntity>(greenId, redLayer, lcad::Point2D(0, 0), lcad::Point2D(2, 2));
    green->setColorOverride(lcad::Color{0, 255, 0}); // ACI 3, no entry
    doc.addEntity(std::move(green));
    appearance = doc.plotAppearance(*doc.findEntity(greenId));
    REQUIRE(static_cast<int>(appearance.color.g) == 255);

    // Screening on a CTB row applies to the plotted color.
    lcad::CtbEntry screened;
    screened.aci = 3;
    screened.screening = 50.0;
    doc.saveCtbEntry(screened);
    appearance = doc.plotAppearance(*doc.findEntity(greenId));
    REQUIRE(static_cast<int>(appearance.color.r) == 128); // toward white
    REQUIRE(static_cast<int>(appearance.color.g) == 255);

    // saveCtbEntry overwrites by ACI; deleteCtbEntry removes.
    REQUIRE(doc.ctbEntries().size() == 2);
    screened.screening = 25.0;
    doc.saveCtbEntry(screened);
    REQUIRE(doc.ctbEntries().size() == 2);
    REQUIRE(doc.findCtbEntry(3)->screening == Approx(25.0));
    REQUIRE(doc.deleteCtbEntry(3));
    REQUIRE_FALSE(doc.deleteCtbEntry(3));
}
