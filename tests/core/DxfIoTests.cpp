#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/PointEnt.h"
#include "core/geometry/ConstructionLine.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Image.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Junction.h"
#include "core/geometry/NetLabel.h"
#include "core/geometry/NoConnect.h"
#include "core/geometry/PointCloud.h"
#include "core/geometry/Leader.h"
#include "core/geometry/Line.h"
#include "core/geometry/MLeader.h"
#include "core/geometry/MText.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Table.h"
#include "core/geometry/Text.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/geometry/Wipeout.h"
#include "core/geometry/Wire.h"
#include "core/io/DxfColors.h"
#include "core/io/DxfReader.h"
#include "core/io/DxfWriter.h"
#include "core/io/PointCloudFile.h"
#include "core/io/Xref.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

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
        case lcad::EntityType::MText:
        case lcad::EntityType::Leader:
        case lcad::EntityType::MLeader:
        case lcad::EntityType::Spline:
        case lcad::EntityType::Point:
        case lcad::EntityType::ConstructionLine:
        case lcad::EntityType::AttDef:
        case lcad::EntityType::Table:
        case lcad::EntityType::Image:
        case lcad::EntityType::PointCloud:
        case lcad::EntityType::Wire:
        case lcad::EntityType::Junction:
        case lcad::EntityType::NoConnect:
        case lcad::EntityType::NetLabel:
        case lcad::EntityType::Track:
        case lcad::EntityType::Via:
        case lcad::EntityType::Wipeout:
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

TEST_CASE("DXF gradient hatch round-trips", "[dxf][hatch][gradient]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<lcad::Point2D> tri{{0, 0}, {10, 0}, {5, 8}};
    auto hatch = std::make_unique<lcad::HatchEntity>(doc.reserveEntityId(), doc.currentLayer(), tri);
    hatch->setColorOverride(lcad::Color{200, 50, 50});
    hatch->setGradientColor2(lcad::Color{50, 50, 200});
    hatch->setGradientPreset(lcad::GradientPreset::InvHemispherical);
    doc.addEntity(std::move(hatch));
    REQUIRE(lcad::writeDxf(doc, temp.path.string()));

    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));
    REQUIRE(loaded.entities().size() == 1);

    const auto* loadedHatch = static_cast<const lcad::HatchEntity*>(loaded.entities().front());
    REQUIRE(loadedHatch->isGradient());
    REQUIRE(loadedHatch->gradientColor2()->r == 50);
    REQUIRE(loadedHatch->gradientColor2()->g == 50);
    REQUIRE(loadedHatch->gradientColor2()->b == 200);
    REQUIRE(loadedHatch->colorOverride()->r == 200);
    REQUIRE(loadedHatch->gradientPreset() == lcad::GradientPreset::InvHemispherical);
}

TEST_CASE("DXF dynamic block linear parameter round-trips", "[dxf][block][dynamic]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<std::unique_ptr<lcad::Entity>> children;
    children.push_back(
        std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0)));
    doc.addBlock("bar", std::move(children));
    lcad::BlockDefinition* block = doc.findBlock("bar");
    lcad::DynamicLinearParameter dp;
    dp.basePoint = lcad::Point2D(0, 0);
    dp.endPoint = lcad::Point2D(10, 0);
    dp.frameMin = lcad::Point2D(8, -1);
    dp.frameMax = lcad::Point2D(12, 1);
    block->dynamicParam = dp;

    auto insert = std::make_unique<lcad::InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), block,
                                                        lcad::Point2D(20, 20));
    insert->setDynamicStretch(5.0);
    doc.addEntity(std::move(insert));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const lcad::BlockDefinition* loadedBlock = loaded.findBlock("bar");
    REQUIRE(loadedBlock != nullptr);
    REQUIRE(loadedBlock->isDynamic());
    REQUIRE(loadedBlock->dynamicParam->endPoint.x == Approx(10.0));
    REQUIRE(loadedBlock->dynamicParam->frameMax.x == Approx(12.0));

    // The insert itself doesn't persist its per-instance stretch (not
    // implemented -- see the DynamicLinearParameter comment); each reload
    // starts at the block's default (unstretched) shape.
    REQUIRE(loaded.entities().size() == 1);
    REQUIRE(loaded.entities().front()->type() == lcad::EntityType::Insert);
}

TEST_CASE("DXF dynamic block flip/rotation/array/lookup/visibility parameters round-trip",
          "[dxf][block][dynamic]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<std::unique_ptr<lcad::Entity>> children;
    const lcad::EntityId lineId = doc.reserveEntityId();
    children.push_back(
        std::make_unique<lcad::LineEntity>(lineId, 0, lcad::Point2D(0, 0), lcad::Point2D(10, 0)));
    const lcad::EntityId circleId = doc.reserveEntityId();
    children.push_back(std::make_unique<lcad::CircleEntity>(circleId, 0, lcad::Point2D(5, 5), 2.0));
    doc.addBlock("multi", std::move(children));
    lcad::BlockDefinition* block = doc.findBlock("multi");

    block->dynamicFlip = lcad::DynamicFlipParameter{lcad::Point2D(0, 0), lcad::Point2D(0, 10)};
    block->dynamicRotation = lcad::DynamicRotationParameter{lcad::Point2D(1, 1), 7.5};
    block->dynamicArray = lcad::DynamicArrayParameter{lcad::Point2D(0, 0), lcad::Point2D(1, 0), 4.0, 1};
    block->dynamicLookup = lcad::DynamicLookupParameter{"Lookup1", {{"Small", 0.5}, {"Large", 2.0}}};
    lcad::DynamicVisibilityParameter vp;
    vp.states = {"Open", "Closed"};
    vp.visibleIds["Open"] = {lineId};
    vp.visibleIds["Closed"] = {circleId};
    block->dynamicVisibility = vp;

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const lcad::BlockDefinition* loadedBlock = loaded.findBlock("multi");
    REQUIRE(loadedBlock != nullptr);
    REQUIRE(loadedBlock->isDynamic());

    REQUIRE(loadedBlock->dynamicFlip.has_value());
    REQUIRE(loadedBlock->dynamicFlip->endPoint.y == Approx(10.0));

    REQUIRE(loadedBlock->dynamicRotation.has_value());
    REQUIRE(loadedBlock->dynamicRotation->basePoint.x == Approx(1.0));
    REQUIRE(loadedBlock->dynamicRotation->defaultRadius == Approx(7.5));

    REQUIRE(loadedBlock->dynamicArray.has_value());
    REQUIRE(loadedBlock->dynamicArray->spacing == Approx(4.0));
    REQUIRE(loadedBlock->dynamicArray->direction.x == Approx(1.0));

    REQUIRE(loadedBlock->dynamicLookup.has_value());
    REQUIRE(loadedBlock->dynamicLookup->presets.size() == 2);
    REQUIRE(loadedBlock->dynamicLookup->presets[0].first == "Small");
    REQUIRE(loadedBlock->dynamicLookup->presets[0].second == Approx(0.5));
    REQUIRE(loadedBlock->dynamicLookup->presets[1].second == Approx(2.0));

    REQUIRE(loadedBlock->dynamicVisibility.has_value());
    REQUIRE(loadedBlock->dynamicVisibility->states.size() == 2);
    REQUIRE(loadedBlock->entities.size() == 2);
    // Reloaded entities get fresh ids, but write/read order is preserved, so
    // entities[0] is the line ("Open") and entities[1] is the circle
    // ("Closed") -- the indices the visibility parameter was keyed on.
    const lcad::EntityId newLineId = loadedBlock->entities[0]->id();
    const lcad::EntityId newCircleId = loadedBlock->entities[1]->id();
    const auto& openIds = loadedBlock->dynamicVisibility->visibleIds.at("Open");
    const auto& closedIds = loadedBlock->dynamicVisibility->visibleIds.at("Closed");
    REQUIRE(std::find(openIds.begin(), openIds.end(), newLineId) != openIds.end());
    REQUIRE(std::find(closedIds.begin(), closedIds.end(), newCircleId) != closedIds.end());
}

TEST_CASE("DXF geographic location round-trips", "[dxf][geo]") {
    TempDxfPath temp;

    lcad::Document doc;
    lcad::GeoLocation geo;
    geo.designPoint = lcad::Point2D(0, 0);
    geo.latitude = 40.7128;
    geo.longitude = -74.0060;
    geo.northRotation = 0.1;
    doc.setGeoLocation(geo);

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.geoLocation().has_value());
    REQUIRE(loaded.geoLocation()->latitude == Approx(40.7128));
    REQUIRE(loaded.geoLocation()->longitude == Approx(-74.0060));
    REQUIRE(loaded.geoLocation()->northRotation == Approx(0.1));

    // A document with none set round-trips to none, not a spurious default.
    lcad::Document plain;
    TempDxfPath temp2;
    REQUIRE(lcad::writeDxf(plain, temp2.path.string()));
    lcad::Document loadedPlain;
    REQUIRE(lcad::readDxf(loadedPlain, temp2.path.string()));
    REQUIRE_FALSE(loadedPlain.geoLocation().has_value());
}

TEST_CASE("DXF point cloud round-trips by re-reading its source file", "[dxf][pointcloud]") {
    TempDxfPath temp;
    const std::filesystem::path xyzPath =
        std::filesystem::temp_directory_path() / ("kumcad_pc_test_" + std::to_string(std::rand()) + ".xyz");
    {
        std::ofstream out(xyzPath, std::ios::binary);
        out << "1 2 0\n3 4 0\n5 6 0\n";
    }

    lcad::Document doc;
    doc.addEntity(std::make_unique<lcad::PointCloudEntity>(
        doc.reserveEntityId(), doc.currentLayer(), xyzPath.string(), lcad::readPointCloudXyz(xyzPath.string())));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    REQUIRE(entities[0]->type() == lcad::EntityType::PointCloud);
    const auto* cloud = static_cast<const lcad::PointCloudEntity*>(entities[0]);
    REQUIRE(cloud->path() == xyzPath.string());
    REQUIRE(cloud->points().size() == 3);
    REQUIRE(cloud->points()[1].x == Approx(3.0));
    REQUIRE(cloud->points()[1].y == Approx(4.0));

    std::filesystem::remove(xyzPath);
}

TEST_CASE("DXF image underlay round-trips", "[dxf][image]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.addEntity(std::make_unique<lcad::ImageEntity>(doc.reserveEntityId(), doc.currentLayer(), "/tmp/site-plan.png",
                                                       lcad::Point2D(5, 10), 40.0, 30.0, M_PI / 6));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    REQUIRE(entities[0]->type() == lcad::EntityType::Image);
    const auto* image = static_cast<const lcad::ImageEntity*>(entities[0]);
    REQUIRE(image->path() == "/tmp/site-plan.png");
    REQUIRE(image->position().x == Approx(5.0));
    REQUIRE(image->position().y == Approx(10.0));
    REQUIRE(image->width() == Approx(40.0));
    REQUIRE(image->height() == Approx(30.0));
    REQUIRE(image->rotation() == Approx(M_PI / 6));
    REQUIRE(image->pdfPage() == 0); // plain raster image: no page concept
}

TEST_CASE("DXF round-trips a PDF underlay's page index", "[dxf][image][pdf]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.addEntity(std::make_unique<lcad::ImageEntity>(doc.reserveEntityId(), doc.currentLayer(), "/tmp/plans.pdf",
                                                       lcad::Point2D(0, 0), 20.0, 15.0, 0.0, 2));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    const auto* image = static_cast<const lcad::ImageEntity*>(entities[0]);
    REQUIRE(image->path() == "/tmp/plans.pdf");
    REQUIRE(image->pdfPage() == 2);
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

TEST_CASE("DXF hatch pattern round-trips", "[dxf][hatch]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<lcad::Point2D> square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    doc.addEntity(std::make_unique<lcad::HatchEntity>(doc.reserveEntityId(), doc.currentLayer(), square,
                                                      lcad::HatchPattern::Ansi33, 2.0, M_PI / 6));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    const auto* hatch = static_cast<const lcad::HatchEntity*>(entities[0]);
    REQUIRE(hatch->pattern() == lcad::HatchPattern::Ansi33);
    REQUIRE(hatch->patternScale() == Approx(2.0));
    REQUIRE(hatch->patternAngle() == Approx(M_PI / 6));
    REQUIRE(hatch->vertices().size() == 4);
}

TEST_CASE("DXF MTEXT round-trips", "[dxf][mtext]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.addEntity(std::make_unique<lcad::MTextEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                      lcad::Point2D(5, 7), "line one\nline two", 2.5, 60.0,
                                                      M_PI / 12));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    REQUIRE(entities[0]->type() == lcad::EntityType::MText);
    const auto* mtext = static_cast<const lcad::MTextEntity*>(entities[0]);
    REQUIRE(mtext->text() == "line one\nline two");
    REQUIRE(mtext->position().x == Approx(5.0));
    REQUIRE(mtext->position().y == Approx(7.0));
    REQUIRE(mtext->height() == Approx(2.5));
    REQUIRE(mtext->width() == Approx(60.0));
    REQUIRE(mtext->rotation() == Approx(M_PI / 12));
}

TEST_CASE("DXF radial/diameter/angular dimensions and dim style round-trip", "[dxf][dimension]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.dimStyle().textHeight = 4.0;
    doc.dimStyle().arrowSize = 2.0;
    doc.dimStyle().decimals = 3;
    doc.addEntity(std::make_unique<lcad::DimensionEntity>(doc.reserveEntityId(), 0, lcad::DimensionKind::Radius,
                                                          lcad::Point2D(0, 0), lcad::Point2D(5, 0),
                                                          lcad::Point2D(3, 0)));
    doc.addEntity(std::make_unique<lcad::DimensionEntity>(doc.reserveEntityId(), 0, lcad::DimensionKind::Diameter,
                                                          lcad::Point2D(10, 10), lcad::Point2D(15, 10),
                                                          lcad::Point2D(12, 10)));
    doc.addEntity(std::make_unique<lcad::DimensionEntity>(doc.reserveEntityId(), 0, lcad::DimensionKind::Angular,
                                                          lcad::Point2D(10, 0), lcad::Point2D(0, 10),
                                                          lcad::Point2D(4, 4), lcad::Point2D(0, 0)));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.dimStyle().textHeight == Approx(4.0));
    REQUIRE(loaded.dimStyle().arrowSize == Approx(2.0));
    REQUIRE(loaded.dimStyle().decimals == 3);

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 3);

    const auto* radius = static_cast<const lcad::DimensionEntity*>(entities[0]);
    REQUIRE(radius->kind() == lcad::DimensionKind::Radius);
    REQUIRE(radius->geometry().value == Approx(5.0));

    const auto* diameter = static_cast<const lcad::DimensionEntity*>(entities[1]);
    REQUIRE(diameter->kind() == lcad::DimensionKind::Diameter);
    REQUIRE(diameter->point1().x == Approx(10.0)); // center reconstructed from the chord
    REQUIRE(diameter->geometry().value == Approx(10.0));

    const auto* angular = static_cast<const lcad::DimensionEntity*>(entities[2]);
    REQUIRE(angular->kind() == lcad::DimensionKind::Angular);
    REQUIRE(angular->vertex().x == Approx(0.0).margin(1e-9));
    REQUIRE(angular->geometry().value == Approx(90.0));
}

TEST_CASE("DXF ordinate/jogged-radius/arc-length dimensions round-trip", "[dxf][dimension]") {
    TempDxfPath temp;

    lcad::Document doc;
    // Ordinate: feature (12,7), leader mostly vertical -> X-type, datum origin (2,3).
    doc.addEntity(std::make_unique<lcad::DimensionEntity>(doc.reserveEntityId(), 0, lcad::DimensionKind::Ordinate,
                                                          lcad::Point2D(12, 7), lcad::Point2D(12, 7),
                                                          lcad::Point2D(12, 17), lcad::Point2D(2, 3)));
    // Jogged: true center (0,0), point on curve (5,0) -> radius 5, override center (100,100).
    doc.addEntity(std::make_unique<lcad::DimensionEntity>(doc.reserveEntityId(), 0, lcad::DimensionKind::Jogged,
                                                          lcad::Point2D(0, 0), lcad::Point2D(5, 0),
                                                          lcad::Point2D(120, 100), lcad::Point2D(100, 100)));
    // ArcLength: center (0,0), quarter circle radius 10.
    doc.addEntity(std::make_unique<lcad::DimensionEntity>(doc.reserveEntityId(), 0, lcad::DimensionKind::ArcLength,
                                                          lcad::Point2D(10, 0), lcad::Point2D(0, 10),
                                                          lcad::Point2D(4, 4), lcad::Point2D(0, 0)));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 3);

    const auto* ordinate = static_cast<const lcad::DimensionEntity*>(entities[0]);
    REQUIRE(ordinate->kind() == lcad::DimensionKind::Ordinate);
    REQUIRE(ordinate->point1().x == Approx(12.0));
    REQUIRE(ordinate->vertex().x == Approx(2.0)); // datum origin survived
    REQUIRE(ordinate->vertex().y == Approx(3.0));
    REQUIRE(ordinate->geometry().value == Approx(12.0 - 2.0));

    const auto* jogged = static_cast<const lcad::DimensionEntity*>(entities[1]);
    REQUIRE(jogged->kind() == lcad::DimensionKind::Jogged);
    REQUIRE(jogged->geometry().value == Approx(5.0)); // true radius, not the override center's distance
    REQUIRE(jogged->vertex().x == Approx(100.0)); // override center survived
    REQUIRE(jogged->vertex().y == Approx(100.0));

    const auto* arcLength = static_cast<const lcad::DimensionEntity*>(entities[2]);
    REQUIRE(arcLength->kind() == lcad::DimensionKind::ArcLength);
    REQUIRE(arcLength->geometry().arcRadius == Approx(10.0));
    REQUIRE(arcLength->geometry().value == Approx(10.0 * M_PI / 2.0));
}

TEST_CASE("DXF leader round-trips", "[dxf][leader]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<lcad::Point2D> pts{{0, 0}, {5, 5}, {10, 5}};
    doc.addEntity(std::make_unique<lcad::LeaderEntity>(doc.reserveEntityId(), doc.currentLayer(), pts, 2.0));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    REQUIRE(entities[0]->type() == lcad::EntityType::Leader);
    const auto* leader = static_cast<const lcad::LeaderEntity*>(entities[0]);
    REQUIRE(leader->points().size() == 3);
    REQUIRE(leader->points()[1].x == Approx(5.0));
    REQUIRE(leader->points()[1].y == Approx(5.0));
}

TEST_CASE("DXF multileader round-trips", "[dxf][mleader]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<std::vector<lcad::Point2D>> legs{{{0, 0}, {3, 3}}};
    doc.addEntity(std::make_unique<lcad::MLeaderEntity>(doc.reserveEntityId(), doc.currentLayer(), legs,
                                                         lcad::Point2D(8, 3), 1.5));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    REQUIRE(entities[0]->type() == lcad::EntityType::MLeader);
    const auto* mleader = static_cast<const lcad::MLeaderEntity*>(entities[0]);
    REQUIRE(mleader->legs().size() == 1);
    REQUIRE(mleader->legs()[0].size() == 2);
    REQUIRE(mleader->legs()[0][1].x == Approx(3.0));
    REQUIRE(mleader->legs()[0][1].y == Approx(3.0));
    REQUIRE(mleader->landing().x == Approx(8.0));
    REQUIRE(mleader->landing().y == Approx(3.0));
    REQUIRE(mleader->arrowSize() == Approx(1.5));
}

TEST_CASE("MLeaderEntity::addLeg appends an independent leg sharing the existing landing (MLEADEREDIT "
         "\"Add leader\")",
         "[mleader]") {
    std::vector<std::vector<lcad::Point2D>> legs{{{0, 0}, {3, 3}}};
    lcad::MLeaderEntity mleader(1, 0, legs, lcad::Point2D(8, 3), 1.5);
    REQUIRE(mleader.legs().size() == 1);

    mleader.addLeg({{10, -2}, {9, 1}});
    REQUIRE(mleader.legs().size() == 2);
    REQUIRE(mleader.legs()[1].size() == 2);
    REQUIRE(mleader.legs()[1][0].x == Approx(10.0));
    REQUIRE(mleader.legs()[1][1].y == Approx(1.0));
    // Landing/arrowSize are untouched -- the new leg shares the SAME
    // landing rather than getting its own.
    REQUIRE(mleader.landing().x == Approx(8.0));
    REQUIRE(mleader.arrowSize() == Approx(1.5));

    // The new leg is really part of the entity's own geometry now: a
    // point on it (not just near the old leg) reports zero distance.
    REQUIRE(mleader.distanceTo(lcad::Point2D(9.5, -0.5)) < 0.1);
}

TEST_CASE("DXF table round-trips", "[dxf][table]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<double> rowHeights{1.0, 1.0};
    std::vector<double> colWidths{2.0, 3.0, 2.5};
    std::vector<std::string> cells{"A1", "B1", "C1", "A2", "", "C2"};
    doc.addEntity(std::make_unique<lcad::TableEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                       lcad::Point2D(1.0, 4.0), rowHeights, colWidths, cells, 2.5));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    REQUIRE(entities[0]->type() == lcad::EntityType::Table);
    const auto* table = static_cast<const lcad::TableEntity*>(entities[0]);
    REQUIRE(table->rows() == 2);
    REQUIRE(table->cols() == 3);
    REQUIRE(table->position().x == Approx(1.0));
    REQUIRE(table->position().y == Approx(4.0));
    REQUIRE(table->colWidths()[1] == Approx(3.0));
    REQUIRE(table->cellText(0, 1) == "B1");
    REQUIRE(table->cellText(1, 0) == "A2");
    REQUIRE(table->cellText(1, 1).empty());
    REQUIRE(table->cellText(1, 2) == "C2");
}

TEST_CASE("DXF layout viewports round-trip via *Paper_Space", "[dxf][layout]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), 0, lcad::Point2D(0, 0),
                                                       lcad::Point2D(100, 100)));
    REQUIRE_FALSE(doc.layouts().empty());
    doc.layouts().front().viewports.push_back(
        lcad::Viewport{lcad::Point2D(148.5, 105.0), 200.0, 150.0, lcad::Point2D(50, 50), 0.5});

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.entities().size() == 1); // the line; the viewport isn't an entity
    REQUIRE(loaded.blocks().empty());       // *Paper_Space isn't a user block
    REQUIRE_FALSE(loaded.layouts().empty());
    const auto& viewports = loaded.layouts().front().viewports;
    REQUIRE(viewports.size() == 1);
    REQUIRE(viewports[0].paperCenter.x == Approx(148.5));
    REQUIRE(viewports[0].paperWidth == Approx(200.0));
    REQUIRE(viewports[0].paperHeight == Approx(150.0));
    REQUIRE(viewports[0].modelCenter.x == Approx(50.0));
    REQUIRE(viewports[0].viewScale == Approx(0.5));
}

TEST_CASE("DXF text and dim styles round-trip", "[dxf][style]") {
    TempDxfPath temp;

    lcad::Document doc;
    lcad::TextStyle title;
    title.name = "Title";
    title.font = "DejaVu Serif";
    title.fixedHeight = 5.0;
    title.widthFactor = 0.8;
    title.obliqueDeg = 15.0;
    title.annotative = true;
    doc.addOrUpdateTextStyle(title);
    doc.setCurrentTextStyle("Title");
    doc.setAnnotationScale(2.5);

    lcad::DimStyle arch;
    arch.textHeight = 3.5;
    arch.arrowSize = 2.0;
    arch.decimals = 1;
    doc.addOrUpdateDimStyle("Arch", arch);
    doc.setCurrentDimStyle("Arch");

    auto text = std::make_unique<lcad::TextEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                                   "styled", 5.0);
    text->setStyleName("Title");
    doc.addEntity(std::move(text));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.currentTextStyleName() == "Title");
    const lcad::TextStyle* style = loaded.findTextStyle("Title");
    REQUIRE(style);
    REQUIRE(style->font == "DejaVu Serif");
    REQUIRE(style->fixedHeight == Approx(5.0));
    REQUIRE(style->widthFactor == Approx(0.8));
    REQUIRE(style->obliqueDeg == Approx(15.0));
    REQUIRE(style->annotative);
    REQUIRE(loaded.annotationScale() == Approx(2.5));

    REQUIRE(loaded.currentDimStyleName() == "Arch");
    REQUIRE(loaded.dimStyle().textHeight == Approx(3.5));
    REQUIRE(loaded.dimStyle().arrowSize == Approx(2.0));
    REQUIRE(loaded.dimStyle().decimals == 1);

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    REQUIRE(static_cast<const lcad::TextEntity*>(entities[0])->styleName() == "Title");
}

TEST_CASE("DXF multiple layouts with paper entities round-trip", "[dxf][layout]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                                     lcad::Point2D(50, 0)));

    doc.layouts()[0].name = "Sheet A";
    doc.layouts()[0].paperWidth = 420.0;
    doc.layouts()[0].paperHeight = 297.0;
    doc.layouts()[0].viewports.push_back(
        lcad::Viewport{lcad::Point2D(150, 100), 120.0, 90.0, lcad::Point2D(25, 0), 2.0});

    lcad::Layout second;
    second.name = "Sheet B";
    second.paperWidth = 210.0;
    second.paperHeight = 297.0;
    doc.layouts().push_back(second);

    // Title-block line on layout 0, a note on layout 1.
    doc.setActiveSpace(0);
    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(10, 10),
                                                     lcad::Point2D(410, 10)));
    doc.setActiveSpace(1);
    doc.addEntity(std::make_unique<lcad::TextEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                     lcad::Point2D(20, 250), "Sheet note", 5.0));
    doc.setActiveSpace(-1);

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.layouts().size() == 2);
    REQUIRE(loaded.layouts()[0].name == "Sheet A");
    REQUIRE(loaded.layouts()[0].paperWidth == Approx(420.0));
    REQUIRE(loaded.layouts()[0].paperHeight == Approx(297.0));
    REQUIRE(loaded.layouts()[0].viewports.size() == 1);
    REQUIRE(loaded.layouts()[0].viewports[0].viewScale == Approx(2.0));
    REQUIRE(loaded.layouts()[1].name == "Sheet B");
    REQUIRE(loaded.layouts()[1].paperWidth == Approx(210.0));

    // Model space still has exactly the one model line.
    REQUIRE(loaded.entities().size() == 1);

    const auto paper0 = loaded.paperEntities(0);
    REQUIRE(paper0.size() == 1);
    REQUIRE(paper0[0]->type() == lcad::EntityType::Line);
    const auto paper1 = loaded.paperEntities(1);
    REQUIRE(paper1.size() == 1);
    REQUIRE(paper1[0]->type() == lcad::EntityType::Text);
    REQUIRE(static_cast<const lcad::TextEntity*>(paper1[0])->text() == "Sheet note");
}

TEST_CASE("Xref attach, DXF round-trip of the cached snapshot, and reload", "[dxf][xref]") {
    TempDxfPath external;
    TempDxfPath host;

    // The referenced drawing: one line on a colored layer.
    {
        lcad::Document ref;
        const lcad::LayerId red = ref.addLayer("Red", lcad::Color{255, 0, 0});
        ref.addEntity(std::make_unique<lcad::LineEntity>(ref.reserveEntityId(), red, lcad::Point2D(0, 0),
                                                         lcad::Point2D(30, 0)));
        REQUIRE(lcad::writeDxf(ref, external.path.string()));
    }

    lcad::Document doc;
    std::string error;
    const lcad::BlockDefinition* block = lcad::attachXref(doc, external.path.string(), &error);
    REQUIRE(block);
    REQUIRE(block->isXref());
    REQUIRE(block->entities.size() == 1);
    // Source layer preserved as an xref-bound layer ("<xrefName>|Red") in the
    // host document, not flattened to layer 0 with a baked color override.
    REQUIRE_FALSE(block->entities[0]->colorOverride().has_value());
    const lcad::LayerId xrefLayerId = block->entities[0]->layer();
    REQUIRE(xrefLayerId != 0);
    const lcad::Layer* xrefLayer = doc.findLayer(xrefLayerId);
    REQUIRE(xrefLayer);
    REQUIRE(xrefLayer->name == block->name + "|Red");
    REQUIRE(xrefLayer->color.r == 255);
    REQUIRE(xrefLayer->color.g == 0);
    REQUIRE(xrefLayer->color.b == 0);

    doc.addEntity(std::make_unique<lcad::InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), block,
                                                       lcad::Point2D(100, 100)));

    REQUIRE(lcad::writeDxf(doc, host.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, host.path.string()));

    const lcad::BlockDefinition* loadedBlock = loaded.findBlock(block->name);
    REQUIRE(loadedBlock);
    REQUIRE(loadedBlock->isXref());
    REQUIRE(loadedBlock->xrefPath == external.path.string());
    REQUIRE(loadedBlock->entities.size() == 1); // cached snapshot survives in the file

    // The external file grows a circle; reload picks it up.
    {
        lcad::Document ref;
        ref.addEntity(std::make_unique<lcad::LineEntity>(ref.reserveEntityId(), ref.currentLayer(),
                                                         lcad::Point2D(0, 0), lcad::Point2D(30, 0)));
        ref.addEntity(std::make_unique<lcad::CircleEntity>(ref.reserveEntityId(), ref.currentLayer(),
                                                           lcad::Point2D(10, 10), 4.0));
        REQUIRE(lcad::writeDxf(ref, external.path.string()));
    }
    REQUIRE(lcad::reloadXref(loaded, block->name, &error));
    REQUIRE(loaded.findBlock(block->name)->entities.size() == 2);

    // Reload reuses the xref-bound layer by name instead of duplicating it.
    int redLayerCount = 0;
    for (const lcad::Layer& l : loaded.layers()) {
        if (l.name == block->name + "|Red") ++redLayerCount;
    }
    REQUIRE(redLayerCount == 1);

    // reloadAllXrefs also refreshes (and reports) reachable references.
    REQUIRE(lcad::reloadAllXrefs(loaded, "") == 1);
}

TEST_CASE("DXF round-trips points, construction lines, lineweights, and point style", "[dxf][point][xline]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.setPointMode(34); // circled plus
    doc.setPointSize(3.5);
    doc.addEntity(std::make_unique<lcad::PointEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                      lcad::Point2D(4, 5)));
    doc.addEntity(std::make_unique<lcad::ConstructionLineEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                                 lcad::Point2D(0, 0), lcad::Point2D(1, 1), false));
    doc.addEntity(std::make_unique<lcad::ConstructionLineEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                                 lcad::Point2D(2, 2), lcad::Point2D(0, 1), true));
    auto heavy = std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                                    lcad::Point2D(9, 0));
    heavy->setLineweightOverride(0.5);
    doc.addEntity(std::move(heavy));
    if (lcad::Layer* layer = doc.findLayer(0)) layer->lineweight = 0.35;

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.pointMode() == 34);
    REQUIRE(loaded.pointSize() == Approx(3.5));
    REQUIRE(loaded.findLayer(0)->lineweight == Approx(0.35));

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 4);
    REQUIRE(entities[0]->type() == lcad::EntityType::Point);
    REQUIRE(static_cast<const lcad::PointEntity*>(entities[0])->position().x == Approx(4.0));

    const auto* xline = static_cast<const lcad::ConstructionLineEntity*>(entities[1]);
    REQUIRE(xline->type() == lcad::EntityType::ConstructionLine);
    REQUIRE_FALSE(xline->isRay());
    REQUIRE(xline->direction().x == Approx(std::sqrt(0.5)));

    const auto* ray = static_cast<const lcad::ConstructionLineEntity*>(entities[2]);
    REQUIRE(ray->isRay());
    REQUIRE(ray->direction().y == Approx(1.0));

    REQUIRE(entities[3]->lineweightOverride().has_value());
    REQUIRE(*entities[3]->lineweightOverride() == Approx(0.5));
}

TEST_CASE("DXF round-trips block attributes (ATTDEF + INSERT/ATTRIB)", "[dxf][attrib]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<std::unique_ptr<lcad::Entity>> blockEnts;
    blockEnts.push_back(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), 0, lcad::Point2D(0, 0),
                                                           lcad::Point2D(20, 0)));
    blockEnts.push_back(std::make_unique<lcad::AttDefEntity>(doc.reserveEntityId(), 0, lcad::Point2D(2, 2), "PARTNO",
                                                             "Part number", "P-000", 2.5));
    const lcad::BlockDefinition* block = doc.addBlock("title", std::move(blockEnts));

    auto insert = std::make_unique<lcad::InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), block,
                                                       lcad::Point2D(50, 50));
    insert->setAttribute("PARTNO", "P-123");
    doc.addEntity(std::move(insert));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const lcad::BlockDefinition* loadedBlock = loaded.findBlock("title");
    REQUIRE(loadedBlock);
    REQUIRE(loadedBlock->entities.size() == 2);
    const lcad::AttDefEntity* attdef = nullptr;
    for (const auto& child : loadedBlock->entities) {
        if (child->type() == lcad::EntityType::AttDef) attdef = static_cast<const lcad::AttDefEntity*>(child.get());
    }
    REQUIRE(attdef);
    REQUIRE(attdef->tag() == "PARTNO");
    REQUIRE(attdef->prompt() == "Part number");
    REQUIRE(attdef->defaultValue() == "P-000");

    const auto entities = loaded.entities();
    REQUIRE(entities.size() == 1);
    const auto* loadedInsert = static_cast<const lcad::InsertEntity*>(entities[0]);
    REQUIRE(loadedInsert->type() == lcad::EntityType::Insert);
    const std::string* value = loadedInsert->attributeValue("PARTNO");
    REQUIRE(value);
    REQUIRE(*value == "P-123");
}

TEST_CASE("DXF round-trips saved layer states", "[dxf][layerstate]") {
    TempDxfPath temp;

    lcad::Document doc;
    const lcad::LayerId wallsLayer = doc.addLayer("Walls", lcad::Color{200, 50, 50});
    doc.findLayer(wallsLayer)->linetype = lcad::LineType::Dashed;
    doc.findLayer(wallsLayer)->lineweight = 0.5;
    doc.saveLayerState("Base");

    doc.findLayer(wallsLayer)->visible = false;
    doc.findLayer(wallsLayer)->locked = true;
    doc.saveLayerState("Off");

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.layerStates().size() == 2);
    REQUIRE(loaded.layerStates()[0].name == "Base");
    REQUIRE(loaded.layerStates()[1].name == "Off");

    const auto& baseEntries = loaded.layerStates()[0].entries;
    REQUIRE(baseEntries.size() == 2); // "0" and "Walls"
    const auto wallsEntry =
        std::find_if(baseEntries.begin(), baseEntries.end(), [&](const lcad::LayerStateEntry& e) { return e.layerId == wallsLayer; });
    REQUIRE(wallsEntry != baseEntries.end());
    REQUIRE(wallsEntry->visible);
    REQUIRE_FALSE(wallsEntry->locked);
    REQUIRE(wallsEntry->linetype == lcad::LineType::Dashed);
    REQUIRE(wallsEntry->lineweight == Approx(0.5));

    const auto& offEntries = loaded.layerStates()[1].entries;
    const auto wallsOff =
        std::find_if(offEntries.begin(), offEntries.end(), [&](const lcad::LayerStateEntry& e) { return e.layerId == wallsLayer; });
    REQUIRE(wallsOff != offEntries.end());
    REQUIRE_FALSE(wallsOff->visible);
    REQUIRE(wallsOff->locked);

    // Applying the round-tripped "Base" state actually restores the layer.
    loaded.applyLayerState(loaded.layerStates()[0]);
    REQUIRE(loaded.findLayer(wallsLayer)->visible);
    REQUIRE_FALSE(loaded.findLayer(wallsLayer)->locked);
}

TEST_CASE("DXF round-trips plot styles and their per-layer assignment", "[dxf][plotstyle]") {
    TempDxfPath temp;

    lcad::Document doc;
    const lcad::LayerId wallsLayer = doc.addLayer("Walls", lcad::Color{200, 50, 50});

    lcad::PlotStyle style;
    style.name = "Print Black";
    style.color = lcad::Color{0, 0, 0};
    style.lineweight = 0.05;
    // linetype deliberately left unset, to check it round-trips as nullopt.
    doc.savePlotStyle(style);
    doc.findLayer(wallsLayer)->plotStyle = "Print Black";

    lcad::PlotStyle bare;
    bare.name = "Unassigned"; // exists in the table but no layer uses it
    doc.savePlotStyle(bare);

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.plotStyles().size() == 2);
    const lcad::PlotStyle* loadedStyle = loaded.findPlotStyle("Print Black");
    REQUIRE(loadedStyle);
    REQUIRE(loadedStyle->color.has_value());
    REQUIRE(loadedStyle->color->r == 0);
    REQUIRE(loadedStyle->lineweight.has_value());
    REQUIRE(loadedStyle->lineweight.value() == Approx(0.05));
    REQUIRE_FALSE(loadedStyle->linetype.has_value());

    REQUIRE(loaded.findPlotStyle("Unassigned") != nullptr);

    const lcad::Layer* loadedWalls = loaded.findLayer(wallsLayer);
    REQUIRE(loadedWalls);
    REQUIRE(loadedWalls->plotStyle == "Print Black");
    REQUIRE(loaded.findLayer(0)->plotStyle.empty()); // layer "0" never had one assigned
}

TEST_CASE("DXF round-trips the frozen layer flag (standard bit 0)", "[dxf][layer]") {
    TempDxfPath temp;

    lcad::Document doc;
    const lcad::LayerId frozenLayer = doc.addLayer("Frozen", lcad::Color{0, 255, 0});
    const lcad::LayerId lockedFrozen = doc.addLayer("Both", lcad::Color{0, 0, 255});
    doc.findLayer(frozenLayer)->frozen = true;
    doc.findLayer(lockedFrozen)->frozen = true;
    doc.findLayer(lockedFrozen)->locked = true;

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.findLayer(frozenLayer)->frozen);
    REQUIRE_FALSE(loaded.findLayer(frozenLayer)->locked);
    REQUIRE(loaded.findLayer(lockedFrozen)->frozen);
    REQUIRE(loaded.findLayer(lockedFrozen)->locked);
    REQUIRE_FALSE(loaded.findLayer(0)->frozen);
}

TEST_CASE("DXF round-trips the CTB table, plot mode, and screening", "[dxf][plotstyle]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.setPlotStyleMode(lcad::PlotStyleMode::ColorDependent);

    lcad::CtbEntry pen1;
    pen1.aci = 1;
    pen1.color = lcad::Color{0, 0, 0};
    pen1.lineweight = 0.5;
    pen1.screening = 40.0;
    doc.saveCtbEntry(pen1);

    lcad::CtbEntry pen3;
    pen3.aci = 3; // only a linetype override, everything else defaulted
    pen3.linetype = lcad::LineType::Dashed;
    doc.saveCtbEntry(pen3);

    // A named style with screening, to check the new group on $KUMCAD_PLOTSTYLE.
    lcad::PlotStyle named;
    named.name = "Half Ink";
    named.screening = 50.0;
    doc.savePlotStyle(named);

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.plotStyleMode() == lcad::PlotStyleMode::ColorDependent);
    REQUIRE(loaded.ctbEntries().size() == 2);

    const lcad::CtbEntry* loaded1 = loaded.findCtbEntry(1);
    REQUIRE(loaded1);
    REQUIRE(loaded1->color.has_value());
    REQUIRE(static_cast<int>(loaded1->color->r) == 0);
    REQUIRE(loaded1->lineweight.has_value());
    REQUIRE(loaded1->lineweight.value() == Approx(0.5));
    REQUIRE(loaded1->screening == Approx(40.0));

    const lcad::CtbEntry* loaded3 = loaded.findCtbEntry(3);
    REQUIRE(loaded3);
    REQUIRE_FALSE(loaded3->color.has_value());
    REQUIRE(loaded3->linetype == lcad::LineType::Dashed);
    REQUIRE(loaded3->screening == Approx(100.0));

    const lcad::PlotStyle* loadedNamed = loaded.findPlotStyle("Half Ink");
    REQUIRE(loadedNamed);
    REQUIRE(loadedNamed->screening == Approx(50.0));

    // A default-mode document must not come back color-dependent.
    lcad::Document plain;
    REQUIRE(lcad::writeDxf(plain, temp.path.string()));
    lcad::Document plainLoaded;
    REQUIRE(lcad::readDxf(plainLoaded, temp.path.string()));
    REQUIRE(plainLoaded.plotStyleMode() == lcad::PlotStyleMode::Named);
    REQUIRE(plainLoaded.ctbEntries().empty());
}

TEST_CASE("DXF round-trips schematic symbols, pins, wires, junctions, and net labels", "[dxf][schematic]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.addBlock("R", {});
    lcad::BlockDefinition* block = doc.findBlock("R");
    block->pins.push_back(lcad::Pin{"1", "1", lcad::PinElectricalType::Passive, lcad::Point2D(0, 0),
                                    lcad::Point2D(-5, 0)});
    block->pins.push_back(lcad::Pin{"2", "2", lcad::PinElectricalType::Power, lcad::Point2D(10, 0),
                                    lcad::Point2D(15, 0)});

    doc.addEntity(std::make_unique<lcad::InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), block,
                                                        lcad::Point2D(0, 0)));
    doc.addEntity(std::make_unique<lcad::WireEntity>(
        doc.reserveEntityId(), doc.currentLayer(), std::vector<lcad::Point2D>{lcad::Point2D(10, 0), lcad::Point2D(30, 0)}));
    doc.addEntity(std::make_unique<lcad::JunctionEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(20, 5)));
    doc.addEntity(std::make_unique<lcad::NoConnectEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0)));
    doc.addEntity(
        std::make_unique<lcad::NetLabelEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(30, 0), "VCC"));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const lcad::BlockDefinition* loadedBlock = loaded.findBlock("R");
    REQUIRE(loadedBlock);
    REQUIRE(loadedBlock->isSymbol());
    REQUIRE(loadedBlock->pins.size() == 2);
    REQUIRE(loadedBlock->pins[0].name == "1");
    REQUIRE(loadedBlock->pins[0].number == "1");
    REQUIRE(loadedBlock->pins[0].electricalType == lcad::PinElectricalType::Passive);
    REQUIRE(loadedBlock->pins[0].position.x == Approx(0.0));
    REQUIRE(loadedBlock->pins[0].stubStart.x == Approx(-5.0));
    REQUIRE(loadedBlock->pins[1].electricalType == lcad::PinElectricalType::Power);
    REQUIRE(loadedBlock->pins[1].position.x == Approx(10.0));

    bool foundWire = false, foundJunction = false, foundNoConnect = false, foundNetLabel = false;
    for (const lcad::Entity* e : loaded.entities()) {
        switch (e->type()) {
        case lcad::EntityType::Wire: {
            const auto* wire = static_cast<const lcad::WireEntity*>(e);
            REQUIRE(wire->vertices().size() == 2);
            REQUIRE(wire->vertices()[1].x == Approx(30.0));
            foundWire = true;
            break;
        }
        case lcad::EntityType::Junction: {
            const auto* junction = static_cast<const lcad::JunctionEntity*>(e);
            REQUIRE(junction->position().y == Approx(5.0));
            foundJunction = true;
            break;
        }
        case lcad::EntityType::NoConnect:
            foundNoConnect = true;
            break;
        case lcad::EntityType::NetLabel: {
            const auto* label = static_cast<const lcad::NetLabelEntity*>(e);
            REQUIRE(label->name() == "VCC");
            foundNetLabel = true;
            break;
        }
        default:
            break;
        }
    }
    REQUIRE(foundWire);
    REQUIRE(foundJunction);
    REQUIRE(foundNoConnect);
    REQUIRE(foundNetLabel);
}

TEST_CASE("DXF round-trips PCB footprint pads, tracks, and vias", "[dxf][pcb]") {
    TempDxfPath temp;

    lcad::Document doc;
    doc.addBlock("R_FP", {});
    lcad::BlockDefinition* block = doc.findBlock("R_FP");
    block->pads.push_back(lcad::Pad{"1", lcad::PadShape::Rect, lcad::Point2D(0, 0), 1.5, 1.5, 0.0});
    block->pads.push_back(lcad::Pad{"2", lcad::PadShape::Round, lcad::Point2D(10, 0), 1.6, 1.6, 0.8});

    doc.addEntity(std::make_unique<lcad::InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), block,
                                                        lcad::Point2D(0, 0)));
    doc.addEntity(std::make_unique<lcad::TrackEntity>(
        doc.reserveEntityId(), doc.currentLayer(), std::vector<lcad::Point2D>{lcad::Point2D(10, 0), lcad::Point2D(30, 0)},
        0.3));
    doc.addEntity(std::make_unique<lcad::ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(30, 0),
                                                    0.6, 0.3));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    const lcad::BlockDefinition* loadedBlock = loaded.findBlock("R_FP");
    REQUIRE(loadedBlock);
    REQUIRE(loadedBlock->isFootprint());
    REQUIRE(loadedBlock->pads.size() == 2);
    REQUIRE(loadedBlock->pads[0].number == "1");
    REQUIRE(loadedBlock->pads[0].shape == lcad::PadShape::Rect);
    REQUIRE(loadedBlock->pads[1].shape == lcad::PadShape::Round);
    REQUIRE(loadedBlock->pads[1].drillDiameter == Approx(0.8));

    bool foundTrack = false, foundVia = false;
    for (const lcad::Entity* e : loaded.entities()) {
        if (e->type() == lcad::EntityType::Track) {
            const auto* track = static_cast<const lcad::TrackEntity*>(e);
            REQUIRE(track->vertices().size() == 2);
            REQUIRE(track->width() == Approx(0.3));
            foundTrack = true;
        } else if (e->type() == lcad::EntityType::Via) {
            const auto* via = static_cast<const lcad::ViaEntity*>(e);
            REQUIRE(via->diameter() == Approx(0.6));
            REQUIRE(via->drillDiameter() == Approx(0.3));
            foundVia = true;
        }
    }
    REQUIRE(foundTrack);
    REQUIRE(foundVia);
}

TEST_CASE("DXF round-trips a WipeoutEntity including showFrame", "[dxf][wipeout]") {
    TempDxfPath temp;

    lcad::Document doc;
    std::vector<lcad::Point2D> verts{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    doc.addEntity(std::make_unique<lcad::WipeoutEntity>(doc.reserveEntityId(), doc.currentLayer(), verts, true));
    doc.addEntity(std::make_unique<lcad::WipeoutEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                        std::vector<lcad::Point2D>{{20, 20}, {30, 20}, {25, 30}},
                                                        false));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    int found = 0;
    for (const lcad::Entity* e : loaded.entities()) {
        if (e->type() != lcad::EntityType::Wipeout) continue;
        const auto& wipeout = static_cast<const lcad::WipeoutEntity&>(*e);
        if (wipeout.vertices().size() == 4) {
            REQUIRE(wipeout.showFrame());
            REQUIRE(wipeout.vertices()[2].x == Approx(10.0));
        } else {
            REQUIRE(wipeout.vertices().size() == 3);
            REQUIRE_FALSE(wipeout.showFrame());
        }
        ++found;
    }
    REQUIRE(found == 2);
}

TEST_CASE("DXF round-trips TEXT's width factor via the real group-41 code", "[dxf][text]") {
    TempDxfPath temp;

    lcad::Document doc;
    auto stretched = std::make_unique<lcad::TextEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 0),
                                                        "Stretched", 2.5);
    stretched->setWidthFactor(1.75);
    doc.addEntity(std::move(stretched));
    // A default (unstretched) text: group 41 must NOT be written for it,
    // so old files stay byte-for-byte unaffected by this field existing.
    doc.addEntity(std::make_unique<lcad::TextEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(0, 10),
                                                     "Plain", 2.5));

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    const std::string raw = [&] {
        std::ifstream in(temp.path);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }();
    // Exactly one "41" group code line among the TEXT entities' own data.
    REQUIRE(raw.find("\n41\n") != std::string::npos);

    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    bool foundStretched = false, foundPlain = false;
    for (const lcad::Entity* e : loaded.entities()) {
        if (e->type() != lcad::EntityType::Text) continue;
        const auto& text = static_cast<const lcad::TextEntity&>(*e);
        if (text.text() == "Stretched") {
            REQUIRE(text.widthFactor() == Approx(1.75));
            foundStretched = true;
        } else if (text.text() == "Plain") {
            REQUIRE(text.widthFactor() == Approx(1.0));
            foundPlain = true;
        }
    }
    REQUIRE(foundStretched);
    REQUIRE(foundPlain);
}

TEST_CASE("DXF round-trips a named GROUP's real membership (previously never persisted at all)",
         "[dxf][group]") {
    // Real bug-shaped gap closed: entity ids are reassigned on load (see
    // DxfReader's own reserveEntityId() sequencing), so this specifically
    // exercises that the group's members survive as the SAME real
    // entities post-reload, not just that a group with the right name
    // and count exists.
    TempDxfPath temp;
    lcad::Document doc;

    const lcad::EntityId line1 = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(line1, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(10, 0)));
    const lcad::EntityId circle1 = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::CircleEntity>(circle1, doc.currentLayer(), lcad::Point2D(20, 20), 5.0));
    // An entity deliberately NOT in the group, interleaved between the
    // two members, so the ordinal remap has to skip over it correctly.
    doc.addEntity(std::make_unique<lcad::LineEntity>(doc.reserveEntityId(), doc.currentLayer(), lcad::Point2D(50, 50),
                                                      lcad::Point2D(60, 60)));
    const lcad::EntityId circle2 = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::CircleEntity>(circle2, doc.currentLayer(), lcad::Point2D(30, 30), 8.0));

    doc.setGroup("MyGroup", {line1, circle1, circle2});

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.groups().size() == 1);
    REQUIRE(loaded.groups()[0].first == "MyGroup");
    REQUIRE(loaded.groups()[0].second.size() == 3);

    // Resolve the loaded group's members back to real entities by
    // geometry (positions are exact and unambiguous here) rather than by
    // id, since ids are reassigned on load -- the whole point of this test.
    bool foundLine = false, foundCircle5 = false, foundCircle8 = false;
    for (lcad::EntityId id : loaded.groups()[0].second) {
        const lcad::Entity* e = loaded.findEntity(id);
        REQUIRE(e != nullptr);
        if (e->type() == lcad::EntityType::Line) {
            const auto& line = static_cast<const lcad::LineEntity&>(*e);
            REQUIRE(line.start().x == Approx(0.0));
            foundLine = true;
        } else if (e->type() == lcad::EntityType::Circle) {
            const auto& circle = static_cast<const lcad::CircleEntity&>(*e);
            if (circle.radius() == Approx(5.0)) foundCircle5 = true;
            if (circle.radius() == Approx(8.0)) foundCircle8 = true;
        }
    }
    REQUIRE(foundLine);
    REQUIRE(foundCircle5);
    REQUIRE(foundCircle8);

    // groupOf must also work post-reload: clicking any member resolves
    // the whole group, the actual UI-facing behavior this persists for.
    const auto* membersOfCircle5 = loaded.groupOf(
        *std::find_if(loaded.groups()[0].second.begin(), loaded.groups()[0].second.end(), [&](lcad::EntityId id) {
            const lcad::Entity* e = loaded.findEntity(id);
            return e && e->type() == lcad::EntityType::Circle &&
                  static_cast<const lcad::CircleEntity*>(e)->radius() == Approx(5.0);
        }));
    REQUIRE(membersOfCircle5 != nullptr);
    REQUIRE(membersOfCircle5->size() == 3);
}

TEST_CASE("DXF round-trips multiple groups and skips a group member deleted before saving",
         "[dxf][group]") {
    TempDxfPath temp;
    lcad::Document doc;

    const lcad::EntityId a = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(a, doc.currentLayer(), lcad::Point2D(0, 0), lcad::Point2D(1, 0)));
    const lcad::EntityId b = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(b, doc.currentLayer(), lcad::Point2D(2, 0), lcad::Point2D(3, 0)));
    const lcad::EntityId c = doc.reserveEntityId();
    doc.addEntity(std::make_unique<lcad::LineEntity>(c, doc.currentLayer(), lcad::Point2D(4, 0), lcad::Point2D(5, 0)));

    doc.setGroup("GroupA", {a, b});
    doc.setGroup("GroupB", {b, c, static_cast<lcad::EntityId>(999999)}); // 999999: dead id, never existed

    REQUIRE(lcad::writeDxf(doc, temp.path.string()));
    lcad::Document loaded;
    REQUIRE(lcad::readDxf(loaded, temp.path.string()));

    REQUIRE(loaded.groups().size() == 2);
    bool foundA = false, foundB = false;
    for (const auto& [name, members] : loaded.groups()) {
        if (name == "GroupA") {
            REQUIRE(members.size() == 2);
            foundA = true;
        } else if (name == "GroupB") {
            // The dead id was never written as a valid ordinal in the
            // first place (see writeDxf's own "member not found, skip"
            // handling), so only the 2 real members round-trip.
            REQUIRE(members.size() == 2);
            foundB = true;
        }
    }
    REQUIRE(foundA);
    REQUIRE(foundB);
}
