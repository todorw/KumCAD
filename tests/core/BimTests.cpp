#include "core/core3d/Bim.h"
#include "core/document/Document.h"
#include "core/geometry/Table.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
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

TEST_CASE("buildBimShapes builds a multi-segment (L-shaped) wall with the expected total volume",
         "[core3d][bim]") {
    BimModel model;
    Wall wall;
    wall.path = {Point2D(0, 0), Point2D(3000, 0), Point2D(3000, 2000)}; // two straight legs, total length 5000
    wall.height = 2700.0;
    wall.thickness = 200.0;
    model.walls.push_back(wall);

    const BimShapes shapes = buildBimShapes(model);
    REQUIRE(shapes.wallShapes.size() == 1);
    REQUIRE_FALSE(shapes.wallShapes[0].IsNull());
    // A symmetric offset around a bent centerline miters exactly (no gap
    // or overlap at the corner), so volume == area * total centerline
    // length holds here just like the single-segment case.
    REQUIRE(volumeOf(shapes.wallShapes[0]) == Approx(5000.0 * 200.0 * 2700.0).epsilon(1e-3));
}

TEST_CASE("buildBimShapes builds a curved wall (semicircular path) with the volume Pappus's theorem predicts",
         "[core3d][bim]") {
    BimModel model;
    Wall wall;
    // bulge = 1.0 is a 180-degree arc (DXF bulge convention: bulge =
    // tan(includedAngle/4)); chord (0,0)-(2000,0) with a semicircular
    // bulge gives centerline radius = chord/2 = 1000.
    wall.path = {Point2D(0, 0), Point2D(2000, 0)};
    wall.bulges = {1.0, 0.0};
    wall.height = 2700.0;
    wall.thickness = 200.0;
    model.walls.push_back(wall);

    const BimShapes shapes = buildBimShapes(model);
    REQUIRE_FALSE(shapes.wallShapes[0].IsNull());
    const double centerlineRadius = 1000.0;
    const double centerlineLength = centerlineRadius * M_PI; // half the circle's circumference
    REQUIRE(volumeOf(shapes.wallShapes[0]) == Approx(centerlineLength * 200.0 * 2700.0).epsilon(1e-3));
}

TEST_CASE("buildBimShapes cuts a door out of the second leg of a multi-segment wall", "[core3d][bim]") {
    BimModel model;
    Wall wall;
    wall.path = {Point2D(0, 0), Point2D(3000, 0), Point2D(3000, 2000)}; // total length 5000
    wall.height = 2700.0;
    wall.thickness = 200.0;
    model.walls.push_back(wall);

    Opening door;
    door.wallIndex = 0;
    door.offsetAlongWall = 3500.0; // 500 into the second leg
    door.width = 900.0;
    door.height = 2100.0;
    model.openings.push_back(door);

    const BimShapes shapes = buildBimShapes(model);
    REQUIRE_FALSE(shapes.wallShapes[0].IsNull());
    const double wallVolume = 5000.0 * 200.0 * 2700.0;
    const double doorVolume = 900.0 * 200.0 * 2100.0;
    REQUIRE(volumeOf(shapes.wallShapes[0]) == Approx(wallVolume - doorVolume).epsilon(1e-2));
}

TEST_CASE("writeIfcLite/readIfcLite round-trips a multi-segment wall's path and bulges", "[core3d][bim]") {
    TempPath temp;
    BimModel model;
    Wall wall;
    wall.path = {Point2D(0, 0), Point2D(3000, 0), Point2D(3000, 2000)};
    wall.bulges = {0.0, 0.25, 0.0};
    wall.height = 2700.0;
    wall.thickness = 200.0;
    model.walls.push_back(wall);

    REQUIRE(writeIfcLite(model, temp.path.string()));

    BimModel loaded;
    REQUIRE(readIfcLite(loaded, temp.path.string()));
    REQUIRE(loaded.walls.size() == 1);
    REQUIRE(loaded.walls[0].path.size() == 3);
    REQUIRE(loaded.walls[0].path[1].x == Approx(3000.0));
    REQUIRE(loaded.walls[0].path[2].y == Approx(2000.0));
    REQUIRE(loaded.walls[0].bulges.size() == 3);
    REQUIRE(loaded.walls[0].bulges[1] == Approx(0.25));

    const double originalVolume = volumeOf(buildBimShapes(model).wallShapes[0]);
    const double loadedVolume = volumeOf(buildBimShapes(loaded).wallShapes[0]);
    REQUIRE(loadedVolume == Approx(originalVolume).epsilon(1e-6));
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

TEST_CASE("buildBimShapes builds a rectangular column with the expected volume and base elevation", "[core3d][bim]") {
    BimModel model;
    Column column;
    column.x = 1000.0;
    column.y = 500.0;
    column.baseElevation = 0.0;
    column.height = 3000.0;
    column.width = 400.0;
    column.depth = 300.0;
    model.columns.push_back(column);

    const BimShapes shapes = buildBimShapes(model);
    REQUIRE(shapes.columnShapes.size() == 1);
    REQUIRE_FALSE(shapes.columnShapes[0].IsNull());
    REQUIRE(volumeOf(shapes.columnShapes[0]) == Approx(400.0 * 300.0 * 3000.0).margin(1.0));

    Bnd_Box bounds;
    BRepBndLib::Add(shapes.columnShapes[0], bounds);
    double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    REQUIRE(zmin == Approx(0.0).margin(1.0));
    REQUIRE(zmax == Approx(3000.0).margin(1.0));
}

TEST_CASE("buildBimShapes builds a round column matching the cylinder volume formula, at its base elevation",
         "[core3d][bim]") {
    BimModel model;
    Column column;
    column.x = 0.0;
    column.y = 0.0;
    column.baseElevation = 1000.0;
    column.height = 2500.0;
    column.width = 500.0; // diameter -- radius 250
    column.round = true;
    model.columns.push_back(column);

    const BimShapes shapes = buildBimShapes(model);
    REQUIRE_FALSE(shapes.columnShapes[0].IsNull());
    const double radius = column.width / 2.0;
    REQUIRE(volumeOf(shapes.columnShapes[0]) == Approx(M_PI * radius * radius * column.height).epsilon(1e-3));

    Bnd_Box bounds;
    BRepBndLib::Add(shapes.columnShapes[0], bounds);
    double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    REQUIRE(zmin == Approx(1000.0).margin(1.0));
}

TEST_CASE("buildBimShapes builds a beam with the expected volume, sitting at its own elevation", "[core3d][bim]") {
    BimModel model;
    Beam beam;
    beam.x1 = 0.0;
    beam.y1 = 0.0;
    beam.x2 = 4000.0;
    beam.y2 = 0.0;
    beam.elevation = 2700.0;
    beam.width = 300.0;
    beam.depth = 400.0;
    model.beams.push_back(beam);

    const BimShapes shapes = buildBimShapes(model);
    REQUIRE(shapes.beamShapes.size() == 1);
    REQUIRE_FALSE(shapes.beamShapes[0].IsNull());
    REQUIRE(volumeOf(shapes.beamShapes[0]) == Approx(4000.0 * 300.0 * 400.0).margin(1.0));

    Bnd_Box bounds;
    BRepBndLib::Add(shapes.beamShapes[0], bounds);
    double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    REQUIRE(zmin == Approx(2700.0).margin(1.0));
    REQUIRE(zmax == Approx(3100.0).margin(1.0));
}

TEST_CASE("combinedBimShape fuses columns and beams into the compound too", "[core3d][bim]") {
    BimModel model;
    Column column;
    column.height = 3000.0;
    model.columns.push_back(column);
    Beam beam;
    beam.x2 = 3000.0;
    model.beams.push_back(beam);

    const BimShapes shapes = buildBimShapes(model);
    const TopoDS_Shape combined = combinedBimShape(shapes);
    REQUIRE_FALSE(combined.IsNull());
}

TEST_CASE("writeIfcLite/readIfcLite round-trips columns, beams, and spaces", "[core3d][bim]") {
    TempPath temp;
    BimModel model;

    Column column;
    column.x = 1000.0;
    column.y = 2000.0;
    column.baseElevation = 0.0;
    column.height = 3000.0;
    column.width = 400.0;
    column.depth = 400.0;
    column.round = true;
    model.columns.push_back(column);

    Beam beam;
    beam.x1 = 0.0;
    beam.y1 = 0.0;
    beam.x2 = 5000.0;
    beam.y2 = 0.0;
    beam.elevation = 2700.0;
    beam.width = 300.0;
    beam.depth = 400.0;
    model.beams.push_back(beam);

    Space space;
    space.name = "Living Room";
    space.boundary = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
    model.spaces.push_back(space);

    REQUIRE(writeIfcLite(model, temp.path.string()));

    BimModel loaded;
    REQUIRE(readIfcLite(loaded, temp.path.string()));

    REQUIRE(loaded.columns.size() == 1);
    REQUIRE(loaded.columns[0].x == Approx(1000.0));
    REQUIRE(loaded.columns[0].height == Approx(3000.0));
    REQUIRE(loaded.columns[0].round);

    REQUIRE(loaded.beams.size() == 1);
    REQUIRE(loaded.beams[0].x2 == Approx(5000.0));
    REQUIRE(loaded.beams[0].elevation == Approx(2700.0));

    REQUIRE(loaded.spaces.size() == 1);
    REQUIRE(loaded.spaces[0].name == "Living Room");
    REQUIRE(loaded.spaces[0].boundary.size() == 4);
    REQUIRE(loaded.spaces[0].boundary[2].first == Approx(4000.0));
}

TEST_CASE("writeIfcLite/readIfcLite round-trips a space name containing an apostrophe",
         "[core3d][bim]") {
    // A raw, unescaped apostrophe would prematurely close the IFCSPACE
    // line's own quoted string (real STEP/IFC syntax) -- writeIfcLite
    // must escape it ('' , STEP's own convention) and readIfcLite must
    // find the REAL closing quote, not the first one it sees.
    TempPath temp;
    BimModel model;
    Space space;
    space.name = "Chef's Kitchen";
    space.boundary = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
    model.spaces.push_back(space);

    REQUIRE(writeIfcLite(model, temp.path.string()));

    BimModel loaded;
    REQUIRE(readIfcLite(loaded, temp.path.string()));
    REQUIRE(loaded.spaces.size() == 1);
    REQUIRE(loaded.spaces[0].name == "Chef's Kitchen");
    REQUIRE(loaded.spaces[0].boundary.size() == 4);
}

TEST_CASE("writeIfcLite/readIfcLite round-trips a space name with multiple consecutive apostrophes",
         "[core3d][bim]") {
    TempPath temp;
    BimModel model;
    Space space;
    space.name = "The ''Great Room''";
    space.boundary = {{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}};
    model.spaces.push_back(space);

    REQUIRE(writeIfcLite(model, temp.path.string()));

    BimModel loaded;
    REQUIRE(readIfcLite(loaded, temp.path.string()));
    REQUIRE(loaded.spaces.size() == 1);
    REQUIRE(loaded.spaces[0].name == "The ''Great Room''");
}

TEST_CASE("buildRoomScheduleTable computes exact area and perimeter for a rectangular space", "[core3d][bim]") {
    BimModel model;
    Space space;
    space.name = "Office";
    space.boundary = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}}; // 4m x 3m rectangle
    model.spaces.push_back(space);

    Document doc2d;
    TableEntity* table = buildRoomScheduleTable(doc2d, model, Point2D(0, 0));
    REQUIRE(table != nullptr);
    REQUIRE(table->rows() == 2); // header + 1 space
    REQUIRE(table->cellText(0, 0) == "Name");
    REQUIRE(table->cellText(1, 0) == "Office");
    REQUIRE(std::stod(table->cellText(1, 1)) == Approx(4000.0 * 3000.0).margin(1.0));
    REQUIRE(std::stod(table->cellText(1, 2)) == Approx(2.0 * (4000.0 + 3000.0)).margin(1.0));
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
