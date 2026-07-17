#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/document/Document.h"
#include "core/document/ExpressTools.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"
#include "core/geometry/Text.h"

#include <algorithm>
#include <memory>

using Catch::Approx;

namespace {
constexpr double kPi = 3.14159265358979323846;
}

TEST_CASE("combineTextEntities joins in reading order", "[expresstools]") {
    // Three lines placed out of order: the middle one first, then bottom,
    // then top. Reading order must come back top-to-bottom.
    lcad::TextEntity middle(1, 0, lcad::Point2D(0, 10), "middle", 2.5);
    lcad::TextEntity bottom(2, 0, lcad::Point2D(0, 5), "bottom", 2.5);
    lcad::TextEntity top(3, 0, lcad::Point2D(0, 15), "top", 3.5, 0.25);

    const auto combined = lcad::combineTextEntities({&middle, &bottom, &top});
    REQUIRE(combined.has_value());
    REQUIRE(combined->text == "top\nmiddle\nbottom");
    // Anchor/height/rotation come from the topmost text.
    REQUIRE(combined->position.y == Approx(15));
    REQUIRE(combined->height == Approx(3.5));
    REQUIRE(combined->rotation == Approx(0.25));

    // Same y: left-to-right.
    lcad::TextEntity left(4, 0, lcad::Point2D(0, 0), "left", 2.5);
    lcad::TextEntity right(5, 0, lcad::Point2D(10, 0), "right", 2.5);
    const auto row = lcad::combineTextEntities({&right, &left});
    REQUIRE(row->text == "left\nright");

    REQUIRE_FALSE(lcad::combineTextEntities({}).has_value());
}

TEST_CASE("applyTcount numbers in reading order but returns input order", "[expresstools]") {
    // Input order: bottom, top -- numbering must give top the start number.
    std::vector<lcad::TCountItem> items = {
        {lcad::Point2D(0, 0), "bottom"},
        {lcad::Point2D(0, 10), "top"},
    };
    auto prefixed = lcad::applyTcount(items, 1, 1, lcad::TCountPlacement::Prefix);
    REQUIRE(prefixed.size() == 2);
    REQUIRE(prefixed[0] == "2 bottom"); // input slot 0 is the LOWER text
    REQUIRE(prefixed[1] == "1 top");

    auto suffixed = lcad::applyTcount(items, 10, 5, lcad::TCountPlacement::Suffix);
    REQUIRE(suffixed[1] == "top 10");
    REQUIRE(suffixed[0] == "bottom 15");

    auto replaced = lcad::applyTcount(items, 0, 2, lcad::TCountPlacement::Replace);
    REQUIRE(replaced[1] == "0");
    REQUIRE(replaced[0] == "2");
}

TEST_CASE("torientRotation flips upside-down text right-side-up", "[expresstools]") {
    REQUIRE(lcad::torientRotation(0.0) == Approx(0.0));
    REQUIRE(lcad::torientRotation(kPi / 4) == Approx(kPi / 4));
    // Exactly 90 degrees stays (right-reading boundary is inclusive).
    REQUIRE(lcad::torientRotation(kPi / 2) == Approx(kPi / 2));
    // Upside down flips by pi.
    REQUIRE(lcad::torientRotation(kPi) == Approx(0.0));
    REQUIRE(lcad::torientRotation(3 * kPi / 4) == Approx(-kPi / 4));
    // -100 degrees (pointing down-left) flips to +80.
    const double minus100 = -100.0 * kPi / 180.0;
    REQUIRE(lcad::torientRotation(minus100) == Approx(80.0 * kPi / 180.0));
    // Multiple turns normalize first.
    REQUIRE(lcad::torientRotation(2 * kPi + kPi) == Approx(0.0));
}

TEST_CASE("breaklinePoints builds a symmetric zigzag", "[expresstools]") {
    const auto points = lcad::breaklinePoints(lcad::Point2D(0, 0), lcad::Point2D(100, 0), 5.0);
    REQUIRE(points.size() == 6);
    REQUIRE(points.front().x == Approx(0));
    REQUIRE(points.back().x == Approx(100));
    // Zigzag straddles the midpoint symmetrically.
    REQUIRE(points[1].x == Approx(45));
    REQUIRE(points[4].x == Approx(55));
    REQUIRE(points[2].y == Approx(5));
    REQUIRE(points[3].y == Approx(-5));
    REQUIRE(points[2].x + points[3].x == Approx(100));

    // Too short for the symbol: empty.
    REQUIRE(lcad::breaklinePoints(lcad::Point2D(0, 0), lcad::Point2D(8, 0), 5.0).empty());
    REQUIRE(lcad::breaklinePoints(lcad::Point2D(0, 0), lcad::Point2D(0, 0), 5.0).empty());
}

TEST_CASE("entityIdsOnLayer and findLayerByName cover model and paper space", "[expresstools]") {
    lcad::Document doc;
    const lcad::LayerId walls = doc.addLayer("Walls", lcad::Color{255, 0, 0});

    const lcad::EntityId onWalls = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(onWalls, walls, lcad::Point2D(0, 0), lcad::Point2D(1, 1)));
    const lcad::EntityId onZero = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(onZero, 0, lcad::Point2D(0, 0), lcad::Point2D(2, 2)));

    // One more on Walls, but in a layout's paper space.
    doc.layouts().push_back(lcad::Layout{});
    doc.setActiveSpace(0);
    const lcad::EntityId onPaper = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(onPaper, walls, lcad::Point2D(0, 0), lcad::Point2D(3, 3)));
    doc.setActiveSpace(-1);

    const auto ids = lcad::entityIdsOnLayer(doc, walls);
    REQUIRE(ids.size() == 2);
    REQUIRE(std::find(ids.begin(), ids.end(), onWalls) != ids.end());
    REQUIRE(std::find(ids.begin(), ids.end(), onPaper) != ids.end());

    REQUIRE(lcad::findLayerByName(doc, "Walls") != nullptr);
    REQUIRE(lcad::findLayerByName(doc, "Walls")->id == walls);
    REQUIRE(lcad::findLayerByName(doc, "Nope") == nullptr);
}

TEST_CASE("deleteLayer removes the record but never layer 0", "[expresstools]") {
    lcad::Document doc;
    const lcad::LayerId walls = doc.addLayer("Walls", lcad::Color{255, 0, 0});
    doc.setCurrentLayer(walls);

    REQUIRE_FALSE(doc.deleteLayer(0));
    REQUIRE(doc.deleteLayer(walls));
    REQUIRE(doc.findLayer(walls) == nullptr);
    REQUIRE(doc.currentLayer() == 0); // current fell back to layer 0
    REQUIRE_FALSE(doc.deleteLayer(walls));
}

TEST_CASE("DXF round-trips the frozen layer flag", "[expresstools][dxf]") {
    // Covered properly in DxfIoTests; here just the struct default.
    lcad::Layer layer;
    REQUIRE_FALSE(layer.frozen);
}

TEST_CASE("instantiate keeps ATTDEFs with KeepDefinitions (EXPLODE vs BURST)", "[expresstools]") {
    lcad::Document doc;
    std::vector<std::unique_ptr<lcad::Entity>> children;
    children.push_back(std::make_unique<lcad::AttDefEntity>(doc.reserveEntityId(), 0, lcad::Point2D(0, 0), "PART",
                                                            "Part name:", "default", 2.5));
    const lcad::BlockDefinition* def = doc.addBlock("TITLE", std::move(children));

    lcad::InsertEntity insert(doc.reserveEntityId(), 0, def, lcad::Point2D(10, 10));
    insert.setAttribute("PART", "Widget");

    // BURST flavor: the value comes through as TEXT.
    const auto burst = insert.instantiate();
    REQUIRE(burst.size() == 1);
    REQUIRE(burst[0]->type() == lcad::EntityType::Text);
    REQUIRE(static_cast<const lcad::TextEntity&>(*burst[0]).text() == "Widget");

    // EXPLODE flavor: the raw ATTDEF comes through, value gone.
    const auto exploded = insert.instantiate(lcad::InsertEntity::AttributeMode::KeepDefinitions);
    REQUIRE(exploded.size() == 1);
    REQUIRE(exploded[0]->type() == lcad::EntityType::AttDef);
}
