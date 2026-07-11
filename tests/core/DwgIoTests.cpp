#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Text.h"
#include "core/io/DwgReader.h"
#include "core/io/DwgWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using Catch::Approx;

namespace {

struct TempDwgPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_dwg_test_" + std::to_string(std::rand()) + ".dwg");
    ~TempDwgPath() { std::filesystem::remove(path); }
};

const lcad::Entity* findByType(const std::vector<lcad::Entity*>& entities, lcad::EntityType type) {
    for (const lcad::Entity* e : entities) {
        if (e->type() == type) return e;
    }
    return nullptr;
}

} // namespace

TEST_CASE("DWG write/read round-trips the core entity set", "[dwg]") {
    if (!lcad::dwgWriteSupportAvailable()) {
        SUCCEED("built without LibreDWG; DWG export not available");
        return;
    }

    TempDwgPath temp;

    lcad::Document doc;
    const lcad::LayerId walls = doc.addLayer("Walls", lcad::Color{200, 50, 50});
    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), walls, lcad::Point2D(0, 0),
                                                     lcad::Point2D(100, 50)));
    doc.addEntity(
        std::make_unique<lcad::CircleEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(20, 20), 7.5));
    doc.addEntity(std::make_unique<lcad::ArcEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                                    5.0, 0.0, M_PI / 2));
    std::vector<lcad::Point2D> verts{{0, 0}, {10, 0}, {10, 10}};
    doc.addEntity(std::make_unique<lcad::PolylineEntity>(doc.reserveEntityId(), walls, verts, true));
    doc.addEntity(std::make_unique<lcad::TextEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(5, 5),
                                                     "dwg out", 2.5));

    std::string error;
    int skipped = 0;
    REQUIRE(lcad::writeDwg(doc, temp.path.string(), &error, &skipped));
    REQUIRE(skipped == 0);

    lcad::Document loaded;
    REQUIRE(lcad::readDwg(loaded, temp.path.string(), &error));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 5);

    const auto* line = static_cast<const lcad::LineEntity*>(findByType(entities, lcad::EntityType::Line));
    REQUIRE(line);
    REQUIRE(line->end().x == Approx(100.0));
    REQUIRE(line->end().y == Approx(50.0));
    const lcad::Layer* lineLayer = loaded.findLayer(line->layer());
    REQUIRE(lineLayer);
    REQUIRE(lineLayer->name == "Walls");

    const auto* circle = static_cast<const lcad::CircleEntity*>(findByType(entities, lcad::EntityType::Circle));
    REQUIRE(circle);
    REQUIRE(circle->radius() == Approx(7.5));

    const auto* arc = static_cast<const lcad::ArcEntity*>(findByType(entities, lcad::EntityType::Arc));
    REQUIRE(arc);
    REQUIRE(arc->radius() == Approx(5.0));

    const auto* pl = static_cast<const lcad::PolylineEntity*>(findByType(entities, lcad::EntityType::Polyline));
    REQUIRE(pl);
    REQUIRE(pl->vertices().size() == 3);
    REQUIRE(pl->closed());

    const auto* text = static_cast<const lcad::TextEntity*>(findByType(entities, lcad::EntityType::Text));
    REQUIRE(text);
    REQUIRE(text->text() == "dwg out");
    REQUIRE(text->height() == Approx(2.5));
}
