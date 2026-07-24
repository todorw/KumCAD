#include "core/document/BlockEdit.h"

#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;
using Catch::Approx;

TEST_CASE("extractBlockForEditing preserves each child entity's own original id", "[blockedit]") {
    Document source;
    const LayerId wallsLayer = source.addLayer("Walls", Color{255, 0, 0});
    std::vector<std::unique_ptr<Entity>> body;
    const EntityId lineId = source.reserveEntityId();
    body.push_back(std::make_unique<LineEntity>(lineId, wallsLayer, Point2D(0, 0), Point2D(10, 0)));
    const EntityId circleId = source.reserveEntityId();
    body.push_back(std::make_unique<CircleEntity>(circleId, wallsLayer, Point2D(5, 5), 2.0));
    source.addBlock("Widget", std::move(body));

    Document editDoc = extractBlockForEditing(source, "Widget");

    REQUIRE(editDoc.entities().size() == 2);
    REQUIRE(editDoc.findEntity(lineId) != nullptr);
    REQUIRE(editDoc.findEntity(lineId)->type() == EntityType::Line);
    REQUIRE(editDoc.findEntity(circleId) != nullptr);
    REQUIRE(editDoc.findEntity(circleId)->type() == EntityType::Circle);

    // A real, independent clone -- editing it must not affect source's own
    // block definition until applyBlockEdit is actually called.
    editDoc.findEntity(lineId)->translate(Point2D(100, 100));
    const auto& stillOriginal = static_cast<const LineEntity&>(*source.findBlock("Widget")->entities[0]);
    REQUIRE(stillOriginal.start().x == Approx(0.0));

    // The copied entity's own layer resolves under the SAME id, keeping
    // its real color/visibility while being edited.
    const Layer* copiedLayer = editDoc.findLayer(wallsLayer);
    REQUIRE(copiedLayer != nullptr);
    REQUIRE(copiedLayer->name == "Walls");
}

TEST_CASE("extractBlockForEditing bumps the next-id counter so a newly drawn entity can't collide",
          "[blockedit]") {
    Document source;
    std::vector<std::unique_ptr<Entity>> body;
    // Force a high id on the block's own child entity by reserving (and
    // discarding) several ids first.
    for (int i = 0; i < 5; ++i) source.reserveEntityId();
    const EntityId highId = source.reserveEntityId();
    body.push_back(std::make_unique<LineEntity>(highId, source.currentLayer(), Point2D(0, 0), Point2D(1, 0)));
    source.addBlock("Widget", std::move(body));

    Document editDoc = extractBlockForEditing(source, "Widget");
    const EntityId freshId = editDoc.reserveEntityId();
    REQUIRE(freshId > highId);
}

TEST_CASE("extractBlockForEditing returns an empty document for an unknown block name", "[blockedit]") {
    Document source;
    const Document editDoc = extractBlockForEditing(source, "NoSuchBlock");
    REQUIRE(editDoc.entities().empty());
}

TEST_CASE("applyBlockEdit writes the edited document's entities back into the SAME BlockDefinition",
          "[blockedit]") {
    Document source;
    std::vector<std::unique_ptr<Entity>> body;
    const EntityId lineId = source.reserveEntityId();
    body.push_back(std::make_unique<LineEntity>(lineId, source.currentLayer(), Point2D(0, 0), Point2D(10, 0)));
    source.addBlock("Widget", std::move(body));
    BlockDefinition* target = source.findBlock("Widget");
    REQUIRE(target->entities.size() == 1);

    Document editDoc = extractBlockForEditing(source, "Widget");
    // Edit the line and add a new circle inside the edit session.
    static_cast<LineEntity*>(editDoc.findEntity(lineId))->translate(Point2D(5, 0));
    editDoc.addEntity(
        std::make_unique<CircleEntity>(editDoc.reserveEntityId(), editDoc.currentLayer(), Point2D(0, 0), 3.0));

    applyBlockEdit(*target, editDoc);

    REQUIRE(target->entities.size() == 2);
    bool sawMovedLine = false;
    bool sawNewCircle = false;
    for (const auto& e : target->entities) {
        if (e->type() == EntityType::Line) {
            sawMovedLine = static_cast<const LineEntity*>(e.get())->start().x == Approx(5.0);
        } else if (e->type() == EntityType::Circle) {
            sawNewCircle = true;
        }
    }
    REQUIRE(sawMovedLine);
    REQUIRE(sawNewCircle);
}
