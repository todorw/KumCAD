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
