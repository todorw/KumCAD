#include "core/document/DocumentExtract.h"

#include "core/geometry/Circle.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace lcad;
using Catch::Approx;

TEST_CASE("extractSubset copies only the requested entities, remapped to fresh ids on a matching-name layer",
         "[documentextract]") {
    Document source;
    const LayerId wallsLayer = source.addLayer("Walls", Color{255, 0, 0});
    const EntityId lineId =
        source.reserveEntityId();
    source.addEntity(std::make_unique<LineEntity>(lineId, wallsLayer, Point2D(0, 0), Point2D(10, 0)));
    const EntityId circleId = source.reserveEntityId();
    source.addEntity(std::make_unique<CircleEntity>(circleId, wallsLayer, Point2D(5, 5), 2.0));

    Document extracted = extractSubset(source, {lineId});

    REQUIRE(extracted.entities().size() == 1);
    Entity* copied = extracted.entities().front();
    REQUIRE(copied->type() == EntityType::Line);

    // A real, independent clone -- moving it must NOT affect source's
    // own entity (proves it's not just a shared/aliased pointer).
    copied->translate(Point2D(100, 100));
    const auto& stillOriginal = static_cast<const LineEntity&>(*source.findEntity(lineId));
    REQUIRE(stillOriginal.start().x == Approx(0.0));

    const Layer* copiedLayer = extracted.findLayer(copied->layer());
    REQUIRE(copiedLayer != nullptr);
    REQUIRE(copiedLayer->name == "Walls");
    REQUIRE(copiedLayer->color.r == 255);
}

TEST_CASE("extractSubset silently skips an id that doesn't resolve to a real entity", "[documentextract]") {
    Document source;
    source.addEntity(std::make_unique<LineEntity>(source.reserveEntityId(), source.currentLayer(), Point2D(0, 0),
                                                   Point2D(1, 1)));
    const Document extracted = extractSubset(source, {9999});
    REQUIRE(extracted.entities().empty());
}

TEST_CASE("extractSubset carries every layer along even if the extracted entities don't touch all of them",
         "[documentextract]") {
    Document source;
    source.addLayer("Unused", Color{0, 255, 0});
    const EntityId lineId = source.reserveEntityId();
    source.addEntity(
        std::make_unique<LineEntity>(lineId, source.currentLayer(), Point2D(0, 0), Point2D(1, 1)));

    const Document extracted = extractSubset(source, {lineId});
    bool sawUnused = false;
    for (const Layer& l : extracted.layers()) {
        if (l.name == "Unused") sawUnused = true;
    }
    REQUIRE(sawUnused); // real WBLOCK carries the whole layer table, not just used layers
}

TEST_CASE("extractSubset re-resolves an INSERT's block pointer into the new document's own copy",
         "[documentextract]") {
    Document source;
    std::vector<std::unique_ptr<Entity>> body;
    body.push_back(std::make_unique<LineEntity>(source.reserveEntityId(), source.currentLayer(), Point2D(0, 0),
                                                Point2D(1, 0)));
    source.addBlock("Widget", std::move(body));
    const BlockDefinition* srcBlock = source.findBlock("Widget");
    REQUIRE(srcBlock);

    const EntityId insertId = source.reserveEntityId();
    auto insert = std::make_unique<InsertEntity>(insertId, source.currentLayer(), srcBlock, Point2D(3, 4), 2.0, 0.0);
    insert->setAttribute("REFDES", "W1");
    source.addEntity(std::move(insert));

    const Document extracted = extractSubset(source, {insertId});
    REQUIRE(extracted.entities().size() == 1);
    const auto* copiedInsert = static_cast<const InsertEntity*>(extracted.entities().front());
    REQUIRE(copiedInsert->block() != nullptr);
    REQUIRE(copiedInsert->block() != srcBlock); // its OWN copy, not a dangling pointer into source
    REQUIRE(copiedInsert->block()->name == "Widget");
    REQUIRE(copiedInsert->block()->entities.size() == 1);
    REQUIRE(copiedInsert->position().x == Approx(3.0));
    REQUIRE(copiedInsert->scaleFactor() == Approx(2.0));
    const std::string* refdes = copiedInsert->attributeValue("REFDES");
    REQUIRE(refdes != nullptr);
    REQUIRE(*refdes == "W1");
}
