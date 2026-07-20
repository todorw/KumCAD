#include "core/document/Document.h"
#include "core/geometry/Arc.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/pcb/BoardOutline.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lcad;
using Catch::Approx;

namespace {
double polygonArea(const std::vector<Point2D>& pts) {
    double sum = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const Point2D& a = pts[i];
        const Point2D& b = pts[(i + 1) % pts.size()];
        sum += a.x * b.y - b.x * a.y;
    }
    return std::abs(sum) / 2.0;
}
} // namespace

TEST_CASE("deriveBoardOutline chains 4 LineEntity segments on Edge.Cuts into a closed rectangle",
         "[pcb][boardoutline]") {
    Document doc;
    const LayerId edgeCuts = doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(0, 0), Point2D(20, 0)));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(20, 0), Point2D(20, 10)));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(20, 10), Point2D(0, 10)));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(0, 10), Point2D(0, 0)));

    const std::vector<Point2D> outline = deriveBoardOutline(doc);
    REQUIRE_FALSE(outline.empty());
    REQUIRE(polygonArea(outline) == Approx(200.0).margin(1e-6));
}

TEST_CASE("deriveBoardOutline chains segments regardless of drawing order or direction", "[pcb][boardoutline]") {
    Document doc;
    const LayerId edgeCuts = doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    // Same rectangle, segments added out of order and with mixed direction.
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(0, 10), Point2D(0, 0)));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(20, 0), Point2D(0, 0)));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(20, 0), Point2D(20, 10)));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(20, 10), Point2D(0, 10)));

    const std::vector<Point2D> outline = deriveBoardOutline(doc);
    REQUIRE_FALSE(outline.empty());
    REQUIRE(polygonArea(outline) == Approx(200.0).margin(1e-6));
}

TEST_CASE("deriveBoardOutline reads a closed PolylineEntity directly as the outline", "[pcb][boardoutline]") {
    Document doc;
    const LayerId edgeCuts = doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), edgeCuts, std::vector<Point2D>{{0, 0}, {30, 0}, {30, 15}, {0, 15}}, true));

    const std::vector<Point2D> outline = deriveBoardOutline(doc);
    REQUIRE_FALSE(outline.empty());
    REQUIRE(polygonArea(outline) == Approx(30.0 * 15.0).margin(1e-6));
}

TEST_CASE("deriveBoardOutline chains a mixed line+arc profile (rounded-corner board)", "[pcb][boardoutline]") {
    // A rectangle with one rounded corner: 3 straight sides + 1 quarter-
    // circle arc, area == full rectangle minus the missing corner square
    // plus the quarter-circle -- same idea as SketchToFaceTests' own
    // stadium-shape check, just via Document entities instead of a Sketch.
    Document doc;
    const LayerId edgeCuts = doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    const double radius = 3.0;
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(0, 0), Point2D(20, 0)));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(20, 0), Point2D(20, 10 - radius)));
    doc.addEntity(std::make_unique<ArcEntity>(doc.reserveEntityId(), edgeCuts, Point2D(20 - radius, 10 - radius), radius,
                                              0.0, M_PI / 2.0));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(20 - radius, 10), Point2D(0, 10)));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(0, 10), Point2D(0, 0)));

    const std::vector<Point2D> outline = deriveBoardOutline(doc);
    REQUIRE_FALSE(outline.empty());
    const double expected = 20.0 * 10.0 - radius * radius + (M_PI * radius * radius / 4.0);
    REQUIRE(polygonArea(outline) == Approx(expected).epsilon(1e-3));
}

TEST_CASE("deriveBoardOutline picks the largest closed loop as the outer boundary", "[pcb][boardoutline]") {
    Document doc;
    const LayerId edgeCuts = doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    // Outer 40x30 board...
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), edgeCuts, std::vector<Point2D>{{0, 0}, {40, 0}, {40, 30}, {0, 30}}, true));
    // ...plus a small internal cutout also drawn on Edge.Cuts.
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), edgeCuts, std::vector<Point2D>{{5, 5}, {10, 5}, {10, 10}, {5, 10}}, true));

    const std::vector<Point2D> outline = deriveBoardOutline(doc);
    REQUIRE(polygonArea(outline) == Approx(40.0 * 30.0).margin(1e-6));
}

TEST_CASE("deriveBoardOutline returns empty when there's no Edge.Cuts layer or geometry", "[pcb][boardoutline]") {
    Document doc;
    REQUIRE(deriveBoardOutline(doc).empty());

    doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    REQUIRE(deriveBoardOutline(doc).empty()); // layer exists but has no geometry
}

TEST_CASE("deriveBoardOutline returns empty for an unclosed chain", "[pcb][boardoutline]") {
    Document doc;
    const LayerId edgeCuts = doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(0, 0), Point2D(20, 0)));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), edgeCuts, Point2D(20, 0), Point2D(20, 10)));
    // Never closes back to (0,0).
    REQUIRE(deriveBoardOutline(doc).empty());
}

TEST_CASE("deriveBoardOutline ignores geometry on other layers", "[pcb][boardoutline]") {
    Document doc;
    const LayerId edgeCuts = doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    const LayerId silkscreen = doc.addLayer("F.SilkS", Color{255, 255, 255});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), edgeCuts, std::vector<Point2D>{{0, 0}, {20, 0}, {20, 10}, {0, 10}}, true));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), silkscreen, Point2D(100, 100), Point2D(200, 200)));

    const std::vector<Point2D> outline = deriveBoardOutline(doc);
    REQUIRE(polygonArea(outline) == Approx(200.0).margin(1e-6));
}

TEST_CASE("pointInKeepout respects blocksCopperPour/blocksAutorouting and layer restriction",
         "[pcb][boardoutline][keepout]") {
    KeepoutZone unrestricted;
    unrestricted.polygon = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    KeepoutZone pourOnly;
    pourOnly.polygon = {{20, 0}, {30, 0}, {30, 10}, {20, 10}};
    pourOnly.blocksAutorouting = false;

    KeepoutZone layerRestricted;
    layerRestricted.polygon = {{40, 0}, {50, 0}, {50, 10}, {40, 10}};
    layerRestricted.layer = 7;

    const std::vector<KeepoutZone> zones = {unrestricted, pourOnly, layerRestricted};

    REQUIRE(pointInKeepout(Point2D(5, 5), 1, zones, true));
    REQUIRE(pointInKeepout(Point2D(5, 5), 1, zones, false));

    REQUIRE(pointInKeepout(Point2D(25, 5), 1, zones, true));
    REQUIRE_FALSE(pointInKeepout(Point2D(25, 5), 1, zones, false)); // pourOnly doesn't block routing

    REQUIRE(pointInKeepout(Point2D(45, 5), 7, zones, true));
    REQUIRE_FALSE(pointInKeepout(Point2D(45, 5), 3, zones, true)); // wrong layer

    REQUIRE_FALSE(pointInKeepout(Point2D(100, 100), 1, zones, true)); // outside every zone
}
