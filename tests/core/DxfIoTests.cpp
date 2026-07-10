#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Text.h"
#include "core/io/DxfColors.h"
#include "core/io/DxfReader.h"
#include "core/io/DxfWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

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
        case lcad::EntityType::Dimension:
        case lcad::EntityType::Hatch:
        case lcad::EntityType::Insert:
            break; // not part of this round-trip; covered by their own tests
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

TEST_CASE("DXF ellipse rotation round-trips", "[dxf][ellipse]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.addEntity(std::make_unique<lcad::EllipseEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                          lcad::Point2D(10, 20), 8.0, 3.0, M_PI / 6));
    // Y-major axis-aligned ellipse: comes back with rotation 0, radii intact.
    doc.addEntity(std::make_unique<lcad::EllipseEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                          lcad::Point2D(-5, 0), 2.0, 9.0));
    REQUIRE(lcad::writeDxf(doc, temp.path.string()));

    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));
    REQUIRE(loaded.entities().size() == 2);

    const auto* rotated = static_cast<const lcad::EllipseEntity*>(loaded.entities()[0]);
    REQUIRE(rotated->center().x == Approx(10.0));
    REQUIRE(rotated->radiusX() == Approx(8.0));
    REQUIRE(rotated->radiusY() == Approx(3.0));
    REQUIRE(rotated->rotation() == Approx(M_PI / 6));

    const auto* tall = static_cast<const lcad::EllipseEntity*>(loaded.entities()[1]);
    REQUIRE(tall->radiusX() == Approx(2.0));
    REQUIRE(tall->radiusY() == Approx(9.0));
    REQUIRE(tall->rotation() == Approx(0.0).margin(1e-9));
}

namespace {

void writeTextFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path);
    out << contents;
}

} // namespace

TEST_CASE("DXF reader parses old-style POLYLINE with VERTEX records", "[dxf][polyline]") {
    TempDxfPath temp;
    writeTextFile(temp.path,
                  "0\nSECTION\n2\nENTITIES\n"
                  "0\nPOLYLINE\n8\n0\n66\n1\n70\n1\n"
                  "0\nVERTEX\n8\n0\n10\n0.0\n20\n0.0\n"
                  "0\nVERTEX\n8\n0\n10\n10.0\n20\n0.0\n"
                  "0\nVERTEX\n8\n0\n10\n10.0\n20\n5.0\n"
                  "0\nSEQEND\n"
                  "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n1\n"
                  "0\nENDSEC\n0\nEOF\n");

    lcad::Document loaded;
    std::string error;
    REQUIRE(lcad::readDxf(loaded, temp.path.string(), &error));
    REQUIRE(loaded.entities().size() == 2);

    const auto* pl = static_cast<const lcad::PolylineEntity*>(loaded.entities()[0]);
    REQUIRE(pl->type() == lcad::EntityType::Polyline);
    REQUIRE(pl->vertices().size() == 3);
    REQUIRE(pl->closed());
    REQUIRE(pl->vertices()[1].x == Approx(10.0));
    REQUIRE(pl->vertices()[2].y == Approx(5.0));

    // The entity after SEQEND still parses normally.
    REQUIRE(loaded.entities()[1]->type() == lcad::EntityType::Line);
}

TEST_CASE("DXF reader uses ACI color when a layer has no true color", "[dxf][color]") {
    TempDxfPath temp;
    writeTextFile(temp.path,
                  "0\nSECTION\n2\nTABLES\n"
                  "0\nTABLE\n2\nLAYER\n70\n2\n"
                  "0\nLAYER\n2\nRedLayer\n70\n0\n62\n1\n"
                  "0\nLAYER\n2\nOffBlue\n70\n0\n62\n-5\n"
                  "0\nENDTAB\n0\nENDSEC\n"
                  "0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n");

    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const lcad::Layer* red = nullptr;
    const lcad::Layer* blue = nullptr;
    for (const lcad::Layer& l : loaded.layers()) {
        if (l.name == "RedLayer") red = &l;
        if (l.name == "OffBlue") blue = &l;
    }
    REQUIRE(red != nullptr);
    REQUIRE(red->color.r == 255);
    REQUIRE(red->color.g == 0);
    REQUIRE(red->color.b == 0);
    REQUIRE(red->visible);

    REQUIRE(blue != nullptr);
    REQUIRE(blue->color.b == 255); // ACI 5 = blue, negative = layer off
    REQUIRE_FALSE(blue->visible);
}

TEST_CASE("ACI color mapping round-trips the classic colors", "[dxf][color]") {
    REQUIRE(lcad::colorToAci(lcad::Color{255, 0, 0}) == 1);
    REQUIRE(lcad::colorToAci(lcad::Color{255, 255, 0}) == 2);
    REQUIRE(lcad::colorToAci(lcad::Color{0, 0, 255}) == 5);
    const lcad::Color white = lcad::aciToColor(7);
    REQUIRE(white.r == 255);
    REQUIRE(white.g == 255);
    // Out-of-range indices fall back to white instead of reading off the table.
    const lcad::Color fallback = lcad::aciToColor(999);
    REQUIRE(fallback.r == 255);
}

TEST_CASE("DXF dimension round-trips", "[dxf][dimension]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.addEntity(std::make_unique<lcad::DimensionEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                            lcad::Point2D(0, 0), lcad::Point2D(10, 0),
                                                            lcad::Point2D(5, 4), false, 3.0));
    doc.addEntity(std::make_unique<lcad::DimensionEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                            lcad::Point2D(1, 1), lcad::Point2D(4, 5),
                                                            lcad::Point2D(0, 3), true));
    REQUIRE(lcad::writeDxf(doc, temp.path.string()));

    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));
    REQUIRE(loaded.entities().size() == 2);

    const auto* linear = static_cast<const lcad::DimensionEntity*>(loaded.entities()[0]);
    REQUIRE(linear->type() == lcad::EntityType::Dimension);
    REQUIRE_FALSE(linear->aligned());
    REQUIRE(linear->point1().x == Approx(0.0));
    REQUIRE(linear->point2().x == Approx(10.0));
    REQUIRE(linear->linePoint().y == Approx(4.0));
    REQUIRE(linear->textHeight() == Approx(3.0));
    REQUIRE(linear->geometry().value == Approx(10.0));

    const auto* aligned = static_cast<const lcad::DimensionEntity*>(loaded.entities()[1]);
    REQUIRE(aligned->aligned());
    REQUIRE(aligned->geometry().value == Approx(5.0));
}

TEST_CASE("DXF hatch round-trips", "[dxf][hatch]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<lcad::Point2D> tri{{0, 0}, {10, 0}, {5, 8}};
    doc.addEntity(std::make_unique<lcad::HatchEntity>(doc.reserveEntityId(), doc.currentLayer(), tri));
    REQUIRE(lcad::writeDxf(doc, temp.path.string()));

    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));
    REQUIRE(loaded.entities().size() == 1);

    const auto* hatch = static_cast<const lcad::HatchEntity*>(loaded.entities().front());
    REQUIRE(hatch->type() == lcad::EntityType::Hatch);
    REQUIRE(hatch->vertices().size() == 3);
    REQUIRE(hatch->vertices()[2].y == Approx(8.0));
    REQUIRE(hatch->containsPoint(lcad::Point2D(5, 2)));
}

TEST_CASE("DXF block definitions and inserts round-trip", "[dxf][block]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<std::unique_ptr<lcad::Entity>> children;
    children.push_back(
        std::make_unique<lcad::CircleEntity>(doc.reserveEntityId(), 0, lcad::Point2D(0, 0), 2.0));
    children.push_back(
        std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), 0, lcad::Point2D(-2, 0), lcad::Point2D(2, 0)));
    const lcad::BlockDefinition* block = doc.addBlock("Bolt", std::move(children));
    doc.addEntity(std::make_unique<lcad::InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), block,
                                                         lcad::Point2D(50, 20), 2.0, M_PI / 2));
    REQUIRE(lcad::writeDxf(doc, temp.path.string()));

    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.blocks().size() == 1);
    const lcad::BlockDefinition* loadedBlock = loaded.findBlock("Bolt");
    REQUIRE(loadedBlock != nullptr);
    REQUIRE(loadedBlock->entities.size() == 2);

    REQUIRE(loaded.entities().size() == 1);
    const auto* insert = static_cast<const lcad::InsertEntity*>(loaded.entities().front());
    REQUIRE(insert->type() == lcad::EntityType::Insert);
    REQUIRE(insert->blockName() == "Bolt");
    REQUIRE(insert->position().x == Approx(50.0));
    REQUIRE(insert->scaleFactor() == Approx(2.0));
    REQUIRE(insert->rotation() == Approx(M_PI / 2));
    // The placed circle: radius 2 scaled by 2 around (50, 20).
    const auto box = insert->boundingBox();
    REQUIRE(box.max.x == Approx(54.0));
    REQUIRE(box.min.x == Approx(46.0));
}

TEST_CASE("DXF entity color override round-trips", "[dxf][color]") {
    TempDxfPath temp;

    lcad::Document doc;
    auto line = std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                                     lcad::Point2D(5, 5));
    line->setColorOverride(lcad::Color{10, 200, 30});
    doc.addEntity(std::move(line));
    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(1, 1),
                                                       lcad::Point2D(2, 2))); // ByLayer
    REQUIRE(lcad::writeDxf(doc, temp.path.string()));

    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));
    REQUIRE(loaded.entities().size() == 2);

    const auto& overridden = *loaded.entities()[0];
    REQUIRE(overridden.colorOverride().has_value());
    REQUIRE(overridden.colorOverride()->r == 10);
    REQUIRE(overridden.colorOverride()->g == 200);
    REQUIRE(overridden.colorOverride()->b == 30);

    REQUIRE_FALSE(loaded.entities()[1]->colorOverride().has_value());
}

TEST_CASE("DXF polyline bulges round-trip", "[dxf][bulge]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<lcad::Point2D> verts{{0, 0}, {10, 0}, {10, 10}};
    std::vector<double> bulges{0.5, -1.0, 0.25}; // last one matters because the polyline is closed
    doc.addEntity(
        std::make_unique<lcad::PolylineEntity>(doc.reserveEntityId(), doc.currentLayer(), verts, bulges, true));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    const auto* pl = static_cast<const lcad::PolylineEntity*>(entities[0]);
    REQUIRE(pl->closed());
    REQUIRE(pl->vertices().size() == 3);
    REQUIRE(pl->bulgeAt(0) == Approx(0.5));
    REQUIRE(pl->bulgeAt(1) == Approx(-1.0));
    REQUIRE(pl->bulgeAt(2) == Approx(0.25));
}

TEST_CASE("DXF reader picks up bulges on old-style POLYLINE vertices", "[dxf][bulge][polyline]") {
    TempDxfPath temp;
    {
        std::ofstream out(temp.path);
        out << "0\nSECTION\n2\nENTITIES\n";
        out << "0\nPOLYLINE\n8\n0\n66\n1\n70\n0\n";
        out << "0\nVERTEX\n8\n0\n10\n0\n20\n0\n42\n1.0\n";
        out << "0\nVERTEX\n8\n0\n10\n5\n20\n0\n";
        out << "0\nSEQEND\n";
        out << "0\nENDSEC\n0\nEOF\n";
    }

    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));
    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    const auto* pl = static_cast<const lcad::PolylineEntity*>(entities[0]);
    REQUIRE(pl->vertices().size() == 2);
    REQUIRE(pl->bulgeAt(0) == Approx(1.0));
    REQUIRE(pl->bulgeAt(1) == Approx(0.0));
}

TEST_CASE("DXF linetypes round-trip on layers and entities", "[dxf][linetype]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.setLineTypeScale(2.5);
    const lcad::LayerId hiddenLayer = doc.addLayer("HiddenStuff", lcad::Color{0, 255, 0});
    if (lcad::Layer* layer = doc.findLayer(hiddenLayer)) layer->linetype = lcad::LineType::Hidden;

    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), hiddenLayer, lcad::Point2D(0, 0),
                                                       lcad::Point2D(10, 0)));
    auto dashed = std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 5),
                                                     lcad::Point2D(10, 5));
    dashed->setLinetypeOverride(lcad::LineType::Dashed);
    doc.addEntity(std::move(dashed));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.lineTypeScale() == Approx(2.5));

    const lcad::Layer* layer = nullptr;
    for (const auto& l : loaded.layers()) {
        if (l.name == "HiddenStuff") layer = &l;
    }
    REQUIRE(layer != nullptr);
    REQUIRE(layer->linetype == lcad::LineType::Hidden);

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 2);
    REQUIRE_FALSE(entities[0]->linetypeOverride().has_value()); // ByLayer
    REQUIRE(entities[1]->linetypeOverride().has_value());
    REQUIRE(*entities[1]->linetypeOverride() == lcad::LineType::Dashed);
}

TEST_CASE("Linetype names map both ways", "[linetype]") {
    REQUIRE(std::string(lcad::lineTypeName(lcad::LineType::Dashed)) == "DASHED");
    REQUIRE(lcad::lineTypeFromName("dashed") == lcad::LineType::Dashed);
    REQUIRE(lcad::lineTypeFromName("CENTER") == lcad::LineType::Center);
    REQUIRE_FALSE(lcad::lineTypeFromName("BYLAYER").has_value());
    REQUIRE_FALSE(lcad::lineTypeFromName("NOSUCH").has_value());
    REQUIRE_FALSE(lcad::lineTypePattern(lcad::LineType::Continuous).size());
    REQUIRE(lcad::lineTypePattern(lcad::LineType::DashDot).size() == 4);
}

TEST_CASE("DXF spline round-trips exactly (control points + knots + fit points)", "[dxf][spline]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<lcad::Point2D> fit{{0, 0}, {10, 8}, {20, -3}, {30, 5}};
    auto spline = lcad::SplineEntity::fromFitPoints(doc.reserveEntityId(), doc.currentLayer(), fit);
    REQUIRE(spline != nullptr);
    const auto originalControl = spline->controlPoints();
    const auto originalKnots = spline->knots();
    doc.addEntity(std::move(spline));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    REQUIRE(entities[0]->type() == lcad::EntityType::Spline);
    const auto* loadedSpline = static_cast<const lcad::SplineEntity*>(entities[0]);
    REQUIRE(loadedSpline->degree() == 3);
    REQUIRE(loadedSpline->controlPoints().size() == originalControl.size());
    for (std::size_t i = 0; i < originalControl.size(); ++i) {
        REQUIRE(loadedSpline->controlPoints()[i].x == Approx(originalControl[i].x));
        REQUIRE(loadedSpline->controlPoints()[i].y == Approx(originalControl[i].y));
    }
    REQUIRE(loadedSpline->knots().size() == originalKnots.size());
    for (std::size_t i = 0; i < originalKnots.size(); ++i) {
        REQUIRE(loadedSpline->knots()[i] == Approx(originalKnots[i]).margin(1e-9));
    }
    REQUIRE(loadedSpline->fitPoints().size() == fit.size());
}
