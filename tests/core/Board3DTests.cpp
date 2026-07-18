#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/pcb/Board3D.h"
#include "core/pcb/Stackup.h"
#include "core/schematic/SymbolLibrary.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lcad;
using Catch::Approx;

namespace {
double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}
void zRange(const TopoDS_Shape& shape, double& zMin, double& zMax) {
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    double xmin = 0, ymin = 0, xmax = 0, ymax = 0;
    bounds.Get(xmin, ymin, zMin, xmax, ymax, zMax);
}
} // namespace

TEST_CASE("buildBoard3D builds a substrate with the exact expected volume", "[core3d][board3d]") {
    Document doc;
    Board3DParams params;
    params.boardThickness = 1.6;
    const Board3DShapes shapes = buildBoard3D(doc, {{0, 0}, {50, 0}, {50, 30}, {0, 30}}, {}, params);

    REQUIRE_FALSE(shapes.substrate.IsNull());
    REQUIRE(volumeOf(shapes.substrate) == Approx(50.0 * 30.0 * 1.6).margin(1e-6));
}

TEST_CASE("buildBoard3D rejects a degenerate board outline without crashing", "[core3d][board3d]") {
    Document doc;
    const Board3DShapes shapes = buildBoard3D(doc, {{0, 0}, {50, 0}}, {});
    REQUIRE(shapes.substrate.IsNull());
    REQUIRE(shapes.copper.empty());
    REQUIRE(shapes.components.empty());
}

TEST_CASE("buildBoard3D builds one exact-volume box per track segment, sitting on the top surface without a "
         "stackup",
         "[core3d][board3d]") {
    Document doc;
    Board3DParams params;
    params.boardThickness = 1.6;
    params.copperThickness = 0.035;
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), doc.currentLayer(),
                                                std::vector<Point2D>{Point2D(0, 0), Point2D(10, 0), Point2D(10, 5)},
                                                0.25));

    const Board3DShapes shapes = buildBoard3D(doc, {}, {}, params);
    REQUIRE(shapes.copper.size() == 2); // one box per segment of the 3-point polyline

    double totalVolume = 0.0;
    for (const auto& shape : shapes.copper) totalVolume += volumeOf(shape);
    const double expectedVolume = (10.0 + 5.0) * 0.25 * 0.035; // total track length * width * thickness
    REQUIRE(totalVolume == Approx(expectedVolume).margin(1e-9));

    double zMin = 0, zMax = 0;
    zRange(shapes.copper[0], zMin, zMax);
    REQUIRE(zMin == Approx(params.boardThickness).margin(1e-6)); // no stackup -- everything on the top surface
    REQUIRE(zMax == Approx(params.boardThickness + params.copperThickness).margin(1e-6));
}

TEST_CASE("buildBoard3D spans a through-hole via across the full board thickness", "[core3d][board3d]") {
    Document doc;
    Board3DParams params;
    params.boardThickness = 1.6;
    params.copperThickness = 0.035;
    auto via = std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(5, 5), 0.6, 0.3);
    doc.addEntity(std::move(via));

    const Board3DShapes shapes = buildBoard3D(doc, {}, {}, params);
    REQUIRE(shapes.copper.size() == 1);

    const double radius = 0.3;
    const double height = params.boardThickness + params.copperThickness;
    REQUIRE(volumeOf(shapes.copper[0]) == Approx(M_PI * radius * radius * height).epsilon(1e-3));

    double zMin = 0, zMax = 0;
    zRange(shapes.copper[0], zMin, zMax);
    REQUIRE(zMin == Approx(0.0).margin(1e-6));
    REQUIRE(zMax == Approx(height).margin(1e-6));
}

TEST_CASE("buildBoard3D places a blind via's span using the stackup, not the full board", "[core3d][board3d]") {
    Document doc;
    const LayerId top = doc.addLayer("Top Copper", Color{});
    const LayerId inner = doc.addLayer("Inner1", Color{});
    doc.addLayer("Bottom Copper", Color{});
    const CopperStackup stackup = buildStackup(doc, {"Top Copper", "Inner1", "Bottom Copper"});

    Board3DParams params;
    params.boardThickness = 1.6;
    auto via = std::make_unique<ViaEntity>(doc.reserveEntityId(), doc.currentLayer(), Point2D(5, 5), 0.6, 0.3);
    via->throughHole = false;
    via->fromLayer = top;
    via->toLayer = inner;
    doc.addEntity(std::move(via));

    const Board3DShapes shapes = buildBoard3D(doc, {}, stackup, params);
    REQUIRE(shapes.copper.size() == 1);

    double zMin = 0, zMax = 0;
    zRange(shapes.copper[0], zMin, zMax);
    // Top layer sits at the board's own top surface (1.6); the middle
    // (index 1 of 3) inner layer sits halfway down (0.8) -- the blind via
    // spans only [0.8, 1.6], not down to the board's own bottom (0) the
    // way a through-hole via would.
    REQUIRE(zMin == Approx(0.8).margin(1e-6));
    REQUIRE(zMin > 0.1); // nowhere near the board's own bottom surface
}

TEST_CASE("buildBoard3D builds pad copper and one component placeholder per footprint", "[core3d][board3d]") {
    Document doc;
    registerBuiltinSymbols(doc);
    const BlockDefinition* rfp = doc.findBlock("R_FP");
    auto insert = std::make_unique<InsertEntity>(doc.reserveEntityId(), doc.currentLayer(), rfp, Point2D(0, 0));
    doc.addEntity(std::move(insert));

    const Board3DShapes shapes = buildBoard3D(doc, {}, {});
    REQUIRE(shapes.copper.size() == 2);   // R_FP has 2 pads
    REQUIRE(shapes.components.size() == 1); // one placeholder body for the footprint
    for (const auto& shape : shapes.copper) REQUIRE_FALSE(shape.IsNull());
    REQUIRE_FALSE(shapes.components[0].IsNull());
}

TEST_CASE("buildBoard3D places an SMD footprint's pad copper on its own placement layer, not always the "
         "top",
         "[core3d][board3d]") {
    Document doc;
    registerBuiltinSymbols(doc); // R_FP's pads are SMD (drillDiameter 0)
    doc.addLayer("Top Copper", Color{});
    const LayerId bottom = doc.addLayer("Bottom Copper", Color{});
    const CopperStackup stackup = buildStackup(doc, {"Top Copper", "Bottom Copper"});
    const BlockDefinition* rfp = doc.findBlock("R_FP");

    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), bottom, rfp, Point2D(0, 0)));

    Board3DParams params;
    params.boardThickness = 1.6;
    const Board3DShapes shapes = buildBoard3D(doc, {}, stackup, params);
    REQUIRE(shapes.copper.size() == 2); // one shape per SMD pad, no doubling
    for (const auto& shape : shapes.copper) {
        double zMin = 0, zMax = 0;
        zRange(shape, zMin, zMax);
        REQUIRE(zMin == Approx(0.0).margin(1e-6)); // bottom surface, not the top
    }
}

TEST_CASE("buildBoard3D gives a through-hole footprint pad copper on both the top and bottom surfaces",
         "[core3d][board3d]") {
    Document doc;
    registerBuiltinSymbols(doc); // D_FP's pads are through-hole (drillDiameter 0.8)
    const LayerId top = doc.addLayer("Top Copper", Color{});
    doc.addLayer("Bottom Copper", Color{});
    const CopperStackup stackup = buildStackup(doc, {"Top Copper", "Bottom Copper"});
    const BlockDefinition* dfp = doc.findBlock("D_FP");
    REQUIRE(dfp);

    doc.addEntity(std::make_unique<InsertEntity>(doc.reserveEntityId(), top, dfp, Point2D(0, 0)));

    Board3DParams params;
    params.boardThickness = 1.6;
    const Board3DShapes shapes = buildBoard3D(doc, {}, stackup, params);
    REQUIRE(shapes.copper.size() == 4); // D_FP has 2 pads, each gets a top AND a bottom copper shape

    int atTop = 0, atBottom = 0;
    for (const auto& shape : shapes.copper) {
        double zMin = 0, zMax = 0;
        zRange(shape, zMin, zMax);
        if (std::abs(zMin - 0.0) < 1e-6) ++atBottom;
        if (std::abs(zMin - params.boardThickness) < 1e-6) ++atTop;
    }
    REQUIRE(atTop == 2);
    REQUIRE(atBottom == 2);
}
