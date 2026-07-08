#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Text.h"
#include "core/io/DxfReader.h"
#include "core/io/DxfWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using Catch::Approx;

namespace {

// RAII temp file path: unique per test run, removed on scope exit.
struct TempDxfPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_dxf_test_" + std::to_string(std::rand()) + ".dxf");
    ~TempDxfPath() { std::filesystem::remove(path); }
};

} // namespace

TEST_CASE("DXF round-trip preserves entities and layers", "[dxf]") {
    TempDxfPath temp;

    lcad::Document doc;
    const lcad::LayerId wallsLayer = doc.addLayer("Walls", lcad::Color{200, 50, 50});

    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), wallsLayer, lcad::Point2D(0, 0),
                                                       lcad::Point2D(100, 0)));
    doc.addEntity(
        std::make_unique<lcad::CircleEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(50, 50), 12.5));
    doc.addEntity(std::make_unique<lcad::ArcEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                                      5.0, 0.0, M_PI / 2));
    std::vector<lcad::Point2D> verts{{0, 0}, {10, 0}, {10, 10}};
    doc.addEntity(std::make_unique<lcad::PolylineEntity>(doc.reserveEntityId(), wallsLayer, verts, true));
    doc.addEntity(
        std::make_unique<lcad::EllipseEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(20, 20), 8.0, 3.0));
    doc.addEntity(std::make_unique<lcad::TextEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(5, 5),
                                                       "Hello KumCAD", 2.5, M_PI / 4));

    std::string writeError;
    REQUIRE(lcad::writeDxf(doc, temp.path.string(), &writeError));
    REQUIRE(writeError.empty());

    lcad::Document loaded;
    std::string readError;
    REQUIRE(lcad::readDxf(loaded, temp.path.string(), &readError));
    REQUIRE(readError.empty());

    REQUIRE(loaded.entities().size() == 6);
    REQUIRE(loaded.layers().size() == 2); // default "0" + "Walls"

    const lcad::Layer* loadedWalls = nullptr;
    for (const lcad::Layer& l : loaded.layers()) {
        if (l.name == "Walls") loadedWalls = &l;
    }
    REQUIRE(loadedWalls != nullptr);
    REQUIRE(loadedWalls->color.r == 200);
    REQUIRE(loadedWalls->color.g == 50);
    REQUIRE(loadedWalls->color.b == 50);

    bool foundLine = false;
    bool foundCircle = false;
    bool foundArc = false;
    bool foundPolyline = false;
    bool foundEllipse = false;
    bool foundText = false;
    for (const lcad::Entity* e : loaded.entities()) {
        switch (e->type()) {
        case lcad::EntityType::Line: {
            const auto* line = static_cast<const lcad::LineEntity*>(e);
            REQUIRE(line->start().x == Approx(0.0));
            REQUIRE(line->end().x == Approx(100.0));
            REQUIRE(loaded.findLayer(line->layer())->name == "Walls");
            foundLine = true;
            break;
        }
        case lcad::EntityType::Circle: {
            const auto* circle = static_cast<const lcad::CircleEntity*>(e);
            REQUIRE(circle->center().x == Approx(50.0));
            REQUIRE(circle->radius() == Approx(12.5));
            foundCircle = true;
            break;
        }
        case lcad::EntityType::Arc: {
            const auto* arc = static_cast<const lcad::ArcEntity*>(e);
            REQUIRE(arc->radius() == Approx(5.0));
            REQUIRE(arc->startAngle() == Approx(0.0).margin(1e-6));
            REQUIRE(arc->endAngle() == Approx(M_PI / 2));
            foundArc = true;
            break;
        }
        case lcad::EntityType::Polyline: {
            const auto* pl = static_cast<const lcad::PolylineEntity*>(e);
            REQUIRE(pl->vertices().size() == 3);
            REQUIRE(pl->closed());
            REQUIRE(pl->vertices()[2].x == Approx(10.0));
            foundPolyline = true;
            break;
        }
        case lcad::EntityType::Ellipse: {
            const auto* ellipse = static_cast<const lcad::EllipseEntity*>(e);
            REQUIRE(ellipse->center().x == Approx(20.0));
            REQUIRE(ellipse->radiusX() == Approx(8.0));
            REQUIRE(ellipse->radiusY() == Approx(3.0));
            foundEllipse = true;
            break;
        }
        case lcad::EntityType::Text: {
            const auto* text = static_cast<const lcad::TextEntity*>(e);
            REQUIRE(text->text() == "Hello KumCAD");
            REQUIRE(text->height() == Approx(2.5));
            REQUIRE(text->position().x == Approx(5.0));
            REQUIRE(text->rotation() == Approx(M_PI / 4));
            foundText = true;
            break;
        }
        }
    }
    REQUIRE(foundLine);
    REQUIRE(foundCircle);
    REQUIRE(foundArc);
    REQUIRE(foundPolyline);
    REQUIRE(foundEllipse);
    REQUIRE(foundText);
}

TEST_CASE("DXF read replaces document contents and clears undo history", "[dxf]") {
    TempDxfPath temp;

    lcad::Document source;
    source.addEntity(std::make_unique<lcad::LineEntity>(source.reserveEntityId(), source.currentLayer(),
                                                          lcad::Point2D(1, 1), lcad::Point2D(2, 2)));
    REQUIRE(lcad::writeDxf(source, temp.path.string()));

    lcad::Document target;
    target.addEntity(std::make_unique<lcad::LineEntity>(target.reserveEntityId(), target.currentLayer(),
                                                          lcad::Point2D(9, 9), lcad::Point2D(8, 8)));
    REQUIRE(target.entities().size() == 1);

    REQUIRE(lcad::readDxf(target, temp.path.string()));
    REQUIRE(target.entities().size() == 1);
    REQUIRE_FALSE(target.commandStack().canUndo());

    const auto* line = static_cast<const lcad::LineEntity*>(target.entities().front());
    REQUIRE(line->start().x == Approx(1.0));
}

TEST_CASE("readDxf reports failure for a nonexistent file", "[dxf]") {
    lcad::Document doc;
    std::string error;
    REQUIRE_FALSE(lcad::readDxf(doc, "/nonexistent/path/does_not_exist.dxf", &error));
    REQUIRE_FALSE(error.empty());
}
