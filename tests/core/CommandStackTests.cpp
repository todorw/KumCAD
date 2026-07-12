#include "core/document/Commands.h"
#include "core/document/Document.h"
#include "core/geometry/Line.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("AddEntityCommand undo/redo round-trips", "[commands]") {
    lcad::Document doc;
    const lcad::EntityId id = doc.reserveEntityId();
    auto entity = std::make_unique<lcad::LineEntity>(id, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(1, 1));

    doc.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(doc, std::move(entity)));
    REQUIRE(doc.entities().size() == 1);

    doc.commandStack().undo();
    REQUIRE(doc.entities().empty());

    doc.commandStack().redo();
    REQUIRE(doc.entities().size() == 1);
    REQUIRE(doc.findEntity(id) != nullptr);
}

TEST_CASE("DeleteEntityCommand undo restores the entity", "[commands]") {
    lcad::Document doc;
    const lcad::EntityId id = doc.reserveEntityId();
    auto entity = std::make_unique<lcad::LineEntity>(id, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(1, 1));
    doc.addEntity(std::move(entity));

    doc.commandStack().execute(std::make_unique<lcad::DeleteEntityCommand>(doc, id));
    REQUIRE(doc.entities().empty());

    doc.commandStack().undo();
    REQUIRE(doc.entities().size() == 1);
    REQUIRE(doc.findEntity(id) != nullptr);
}

TEST_CASE("CommandStack clears redo history on new execute", "[commands]") {
    lcad::Document doc;
    const lcad::EntityId id1 = doc.reserveEntityId();
    doc.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(
        doc, std::make_unique<lcad::LineEntity>(id1, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(1, 1))));

    doc.commandStack().undo();
    REQUIRE(doc.commandStack().canRedo());

    const lcad::EntityId id2 = doc.reserveEntityId();
    doc.commandStack().execute(std::make_unique<lcad::AddEntityCommand>(
        doc, std::make_unique<lcad::LineEntity>(id2, doc.currentLayer(), lcad::Point2D(2, 2), lcad::Point2D(3, 3))));

    REQUIRE_FALSE(doc.commandStack().canRedo());
    REQUIRE(doc.entities().size() == 1);
}

TEST_CASE("TranslateEntitiesCommand undo/redo round-trips", "[commands]") {
    lcad::Document doc;
    const lcad::EntityId id = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(1, 1)));

    doc.commandStack().execute(
        std::make_unique<lcad::TranslateEntitiesCommand>(doc, std::vector<lcad::EntityId>{id}, lcad::Point2D(5, 5)));
    auto* line = static_cast<lcad::LineEntity*>(doc.findEntity(id));
    REQUIRE(line->start().x == Catch::Approx(5.0));

    doc.commandStack().undo();
    REQUIRE(line->start().x == Catch::Approx(0.0));

    doc.commandStack().redo();
    REQUIRE(line->start().x == Catch::Approx(5.0));
}

TEST_CASE("MoveGripCommand undo/redo round-trips", "[commands]") {
    lcad::Document doc;
    const lcad::EntityId id = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(10, 0)));
    auto* line = static_cast<lcad::LineEntity*>(doc.findEntity(id));

    doc.commandStack().execute(
        std::make_unique<lcad::MoveGripCommand>(doc, id, 1, line->end(), lcad::Point2D(20, 20)));
    REQUIRE(line->end().x == Catch::Approx(20.0));

    doc.commandStack().undo();
    REQUIRE(line->end().x == Catch::Approx(10.0));
    REQUIRE(line->end().y == Catch::Approx(0.0));

    doc.commandStack().redo();
    REQUIRE(line->end().x == Catch::Approx(20.0));
}

TEST_CASE("RotateEntitiesCommand undo/redo round-trips", "[commands]") {
    lcad::Document doc;
    const lcad::EntityId id = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id, doc.currentLayer(), lcad::Point2D(10, 0), lcad::Point2D(20, 0)));
    auto* line = static_cast<lcad::LineEntity*>(doc.findEntity(id));

    doc.commandStack().execute(std::make_unique<lcad::RotateEntitiesCommand>(
        doc, std::vector<lcad::EntityId>{id}, lcad::Point2D(0, 0), M_PI / 2));
    REQUIRE(line->start().x == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(line->start().y == Catch::Approx(10.0));

    doc.commandStack().undo();
    REQUIRE(line->start().x == Catch::Approx(10.0));
    REQUIRE(line->start().y == Catch::Approx(0.0).margin(1e-9));

    doc.commandStack().redo();
    REQUIRE(line->start().y == Catch::Approx(10.0));
}

TEST_CASE("ScaleEntitiesCommand undo/redo round-trips", "[commands]") {
    lcad::Document doc;
    const lcad::EntityId id = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(10, 0)));
    auto* line = static_cast<lcad::LineEntity*>(doc.findEntity(id));

    doc.commandStack().execute(
        std::make_unique<lcad::ScaleEntitiesCommand>(doc, std::vector<lcad::EntityId>{id}, lcad::Point2D(0, 0), 2.0));
    REQUIRE(line->end().x == Catch::Approx(20.0));

    doc.commandStack().undo();
    REQUIRE(line->end().x == Catch::Approx(10.0));

    doc.commandStack().redo();
    REQUIRE(line->end().x == Catch::Approx(20.0));
}

TEST_CASE("SetEntityLayerCommand undo/redo round-trips with mixed original layers", "[commands]") {
    lcad::Document doc;
    const lcad::LayerId wallsLayer = doc.addLayer("Walls", lcad::Color{255, 0, 0});
    const lcad::LayerId doorsLayer = doc.addLayer("Doors", lcad::Color{0, 255, 0});

    const lcad::EntityId id1 = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id1, wallsLayer, lcad::Point2D(0, 0), lcad::Point2D(1, 1)));
    const lcad::EntityId id2 = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id2, doc.currentLayer(), lcad::Point2D(2, 2), lcad::Point2D(3, 3)));

    doc.commandStack().execute(
        std::make_unique<lcad::SetEntityLayerCommand>(doc, std::vector<lcad::EntityId>{id1, id2}, doorsLayer));
    REQUIRE(doc.findEntity(id1)->layer() == doorsLayer);
    REQUIRE(doc.findEntity(id2)->layer() == doorsLayer);

    doc.commandStack().undo();
    REQUIRE(doc.findEntity(id1)->layer() == wallsLayer); // restored to its own original layer
    REQUIRE(doc.findEntity(id2)->layer() == doc.currentLayer()); // and this one to its (different) original

    doc.commandStack().redo();
    REQUIRE(doc.findEntity(id1)->layer() == doorsLayer);
    REQUIRE(doc.findEntity(id2)->layer() == doorsLayer);
}

TEST_CASE("BatchCommand groups children into one undo step", "[commands]") {
    lcad::Document doc;
    const lcad::EntityId id1 = doc.reserveEntityId();
    const lcad::EntityId id2 = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id1, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(1, 1)));
    doc.addEntity(std::make_unique<lcad::LineEntity>(id2, doc.currentLayer(), lcad::Point2D(2, 2), lcad::Point2D(3, 3)));

    auto batch = std::make_unique<lcad::BatchCommand>("Erase");
    batch->add(std::make_unique<lcad::DeleteEntityCommand>(doc, id1));
    batch->add(std::make_unique<lcad::DeleteEntityCommand>(doc, id2));
    doc.commandStack().execute(std::move(batch));
    REQUIRE(doc.entities().empty());

    doc.commandStack().undo(); // a single undo restores both
    REQUIRE(doc.entities().size() == 2);
    REQUIRE(doc.findEntity(id1) != nullptr);
    REQUIRE(doc.findEntity(id2) != nullptr);
    REQUIRE_FALSE(doc.commandStack().canUndo());

    doc.commandStack().redo();
    REQUIRE(doc.entities().empty());
}

TEST_CASE("SetLayoutsCommand undo/redo round-trips", "[commands][layout]") {
    lcad::Document doc;
    REQUIRE(doc.layouts().size() == 1);

    std::vector<lcad::Layout> newLayouts = doc.layouts();
    lcad::Layout second;
    second.name = "Sheet 2";
    newLayouts.push_back(second);
    doc.commandStack().execute(std::make_unique<lcad::SetLayoutsCommand>(doc, newLayouts));
    REQUIRE(doc.layouts().size() == 2);
    REQUIRE(doc.layouts()[1].name == "Sheet 2");

    doc.commandStack().undo();
    REQUIRE(doc.layouts().size() == 1);

    doc.commandStack().redo();
    REQUIRE(doc.layouts().size() == 2);
    REQUIRE(doc.layouts()[1].name == "Sheet 2");
}

TEST_CASE("RestoreLayerStateCommand undo/redo round-trips", "[commands][layerstate]") {
    lcad::Document doc;
    const lcad::LayerId wallsLayer = doc.addLayer("Walls", lcad::Color{255, 0, 0});
    doc.saveLayerState("Base"); // visible+unlocked

    doc.findLayer(wallsLayer)->visible = false;
    doc.findLayer(wallsLayer)->locked = true;
    doc.saveLayerState("Off"); // hidden+locked

    doc.commandStack().execute(
        std::make_unique<lcad::RestoreLayerStateCommand>(doc, doc.layerStates()[0])); // restore "Base"
    REQUIRE(doc.findLayer(wallsLayer)->visible);
    REQUIRE_FALSE(doc.findLayer(wallsLayer)->locked);

    // Undo puts it back exactly where it was before the restore (hidden+locked),
    // not wherever some other named state happens to leave it.
    doc.commandStack().undo();
    REQUIRE_FALSE(doc.findLayer(wallsLayer)->visible);
    REQUIRE(doc.findLayer(wallsLayer)->locked);

    doc.commandStack().redo();
    REQUIRE(doc.findLayer(wallsLayer)->visible);
    REQUIRE_FALSE(doc.findLayer(wallsLayer)->locked);
}

TEST_CASE("DeleteLayoutCommand undo restores the layout and its entities", "[commands][layout]") {
    lcad::Document doc;
    lcad::Layout second;
    second.name = "Sheet 2";
    doc.layouts().push_back(second);

    doc.setActiveSpace(1);
    const lcad::EntityId id =
        doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(id, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(1, 1)));
    doc.setActiveSpace(-1);
    REQUIRE(doc.layouts()[1].entityIds.size() == 1);
    REQUIRE(doc.findEntity(id) != nullptr);

    doc.commandStack().execute(std::make_unique<lcad::DeleteLayoutCommand>(doc, 1));
    REQUIRE(doc.layouts().size() == 1);
    REQUIRE(doc.findEntity(id) == nullptr);

    doc.commandStack().undo();
    REQUIRE(doc.layouts().size() == 2);
    REQUIRE(doc.layouts()[1].name == "Sheet 2");
    REQUIRE(doc.layouts()[1].entityIds == std::vector<lcad::EntityId>{id});
    REQUIRE(doc.findEntity(id) != nullptr);

    doc.commandStack().redo();
    REQUIRE(doc.layouts().size() == 1);
    REQUIRE(doc.findEntity(id) == nullptr);
}
