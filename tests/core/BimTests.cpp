#include "core/core3d/Bim.h"
#include "core/document/Document.h"
#include "core/geometry/Table.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace lcad;
using Catch::Approx;

namespace {

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

struct TempPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_ifclite_test_" + std::to_string(std::rand()) + ".ifc");
    ~TempPath() { std::filesystem::remove(path); }
};

} // namespace

TEST_CASE("buildBimShapes builds a wall with the expected volume", "[core3d][bim]") {
    BimModel model;
    Wall wall;
    wall.x1 = 0;
    wall.y1 = 0;
    wall.x2 = 5000.0;
    wall.y2 = 0.0;
    wall.height = 2700.0;
    wall.thickness = 200.0;
    model.walls.push_back(wall);

    const BimShapes shapes = buildBimShapes(model);
    REQUIRE(shapes.wallShapes.size() == 1);
    REQUIRE_FALSE(shapes.wallShapes[0].IsNull());
    REQUIRE(volumeOf(shapes.wallShapes[0]) == Approx(5000.0 * 200.0 * 2700.0).margin(1.0));
}

TEST_CASE("buildBimShapes cuts a door opening's volume out of its host wall", "[core3d][bim]") {
    BimModel model;
    Wall wall;
    wall.x1 = 0;
    wall.y1 = 0;
    wall.x2 = 5000.0;
    wall.y2 = 0.0;
    wall.height = 2700.0;
    wall.thickness = 200.0;
    model.walls.push_back(wall);

    Opening door;
    door.wallIndex = 0;
    door.offsetAlongWall = 1000.0;
    door.width = 900.0;
    door.height = 2100.0;
    door.sillHeight = 0.0;
    door.isWindow = false;
    model.openings.push_back(door);

    const BimShapes shapes = buildBimShapes(model);
    REQUIRE_FALSE(shapes.wallShapes[0].IsNull());

    const double wallVolume = 5000.0 * 200.0 * 2700.0;
    const double doorVolume = 900.0 * 200.0 * 2100.0;
    REQUIRE(volumeOf(shapes.wallShapes[0]) == Approx(wallVolume - doorVolume).margin(1.0));
}

TEST_CASE("buildBimShapes handles an angled wall correctly (volume is orientation-independent)", "[core3d][bim]") {
    BimModel model;
    Wall wall;
    wall.x1 = 0;
    wall.y1 = 0;
    wall.x2 = 3000.0;
    wall.y2 = 4000.0; // a 3-4-5 triangle: length 5000
    wall.height = 2500.0;
    wall.thickness = 150.0;
    model.walls.push_back(wall);

    const BimShapes shapes = buildBimShapes(model);
    REQUIRE_FALSE(shapes.wallShapes[0].IsNull());
    REQUIRE(volumeOf(shapes.wallShapes[0]) == Approx(5000.0 * 150.0 * 2500.0).margin(1.0));
}

TEST_CASE("buildBimShapes builds a rectangular slab with the expected volume", "[core3d][bim]") {
    BimModel model;
    Slab slab;
    slab.boundary = {{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}};
    slab.thickness = 150.0;
    slab.elevation = 0.0;
    model.slabs.push_back(slab);

    const BimShapes shapes = buildBimShapes(model);
    REQUIRE(shapes.slabShapes.size() == 1);
    REQUIRE_FALSE(shapes.slabShapes[0].IsNull());
    REQUIRE(volumeOf(shapes.slabShapes[0]) == Approx(6000.0 * 4000.0 * 150.0).margin(1.0));
}

TEST_CASE("combinedBimShape fuses walls and slabs into one non-null compound", "[core3d][bim]") {
    BimModel model;
    Wall wall;
    wall.x2 = 3000.0;
    model.walls.push_back(wall);
    Slab slab;
    slab.boundary = {{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}};
    model.slabs.push_back(slab);

    const BimShapes shapes = buildBimShapes(model);
    const TopoDS_Shape combined = combinedBimShape(shapes);
    REQUIRE_FALSE(combined.IsNull());
}

TEST_CASE("writeIfcLite/readIfcLite round-trips walls, openings, and slabs", "[core3d][bim]") {
    TempPath temp;
    BimModel model;

    Wall wall;
    wall.x1 = 0;
    wall.y1 = 0;
    wall.x2 = 5000.0;
    wall.y2 = 0.0;
    wall.height = 2700.0;
    wall.thickness = 200.0;
    model.walls.push_back(wall);

    Opening window;
    window.wallIndex = 0;
    window.offsetAlongWall = 2000.0;
    window.width = 1200.0;
    window.height = 1200.0;
    window.sillHeight = 900.0;
    window.isWindow = true;
    model.openings.push_back(window);

    Slab slab;
    slab.boundary = {{0, 0}, {5000, 0}, {5000, 4000}, {0, 4000}};
    slab.thickness = 150.0;
    slab.elevation = 2700.0;
    model.slabs.push_back(slab);

    REQUIRE(writeIfcLite(model, temp.path.string()));

    BimModel loaded;
    REQUIRE(readIfcLite(loaded, temp.path.string()));
    REQUIRE(loaded.walls.size() == 1);
    REQUIRE(loaded.walls[0].x2 == Approx(5000.0));
    REQUIRE(loaded.walls[0].thickness == Approx(200.0));
    REQUIRE(loaded.openings.size() == 1);
    REQUIRE(loaded.openings[0].isWindow);
    REQUIRE(loaded.openings[0].sillHeight == Approx(900.0));
    REQUIRE(loaded.slabs.size() == 1);
    REQUIRE(loaded.slabs[0].boundary.size() == 4);
    REQUIRE(loaded.slabs[0].elevation == Approx(2700.0));

    // The round-tripped model builds the same volumes as the original.
    const double originalVolume = volumeOf(buildBimShapes(model).wallShapes[0]);
    const double loadedVolume = volumeOf(buildBimShapes(loaded).wallShapes[0]);
    REQUIRE(loadedVolume == Approx(originalVolume).margin(1.0));
}

TEST_CASE("readIfcLite fails cleanly on a missing file", "[core3d][bim]") {
    BimModel model;
    REQUIRE_FALSE(readIfcLite(model, "/nonexistent/path/kumcad_never_exists.ifc"));
}

TEST_CASE("buildOpeningScheduleTable produces one row per opening plus a header", "[core3d][bim]") {
    BimModel model;
    Wall wall;
    wall.x2 = 5000.0;
    model.walls.push_back(wall);

    Opening door;
    door.wallIndex = 0;
    door.offsetAlongWall = 500.0;
    model.openings.push_back(door);
    Opening window;
    window.wallIndex = 0;
    window.offsetAlongWall = 2000.0;
    window.isWindow = true;
    model.openings.push_back(window);

    Document doc2d;
    TableEntity* table = buildOpeningScheduleTable(doc2d, model, Point2D(0, 0));
    REQUIRE(table != nullptr);
    REQUIRE(table->rows() == 3); // header + 2 openings
    REQUIRE(table->cellText(0, 0) == "Type");
    REQUIRE(table->cellText(1, 0) == "Door");
    REQUIRE(table->cellText(2, 0) == "Window");
}
