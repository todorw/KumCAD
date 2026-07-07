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
