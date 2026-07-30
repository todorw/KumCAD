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

TEST_CASE("deriveBoardOutlineWithHoles keeps the internal cutout deriveBoardOutline itself drops",
         "[pcb][boardoutline]") {
    Document doc;
    const LayerId edgeCuts = doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), edgeCuts, std::vector<Point2D>{{0, 0}, {40, 0}, {40, 30}, {0, 30}}, true));
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), edgeCuts, std::vector<Point2D>{{5, 5}, {10, 5}, {10, 10}, {5, 10}}, true));

    const BoardOutlineWithHoles outline = deriveBoardOutlineWithHoles(doc);
    REQUIRE(polygonArea(outline.boundary) == Approx(40.0 * 30.0).margin(1e-6));
    REQUIRE(outline.holes.size() == 1);
    REQUIRE(polygonArea(outline.holes[0]) == Approx(5.0 * 5.0).margin(1e-6));
}

TEST_CASE("deriveBoardOutlineWithHoles has no holes when Edge.Cuts is just the one boundary loop",
         "[pcb][boardoutline]") {
    Document doc;
    const LayerId edgeCuts = doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), edgeCuts, std::vector<Point2D>{{0, 0}, {40, 0}, {40, 30}, {0, 30}}, true));

    const BoardOutlineWithHoles outline = deriveBoardOutlineWithHoles(doc);
    REQUIRE_FALSE(outline.boundary.empty());
    REQUIRE(outline.holes.empty());
}

TEST_CASE("pointOnBoard is false inside a cutout hole, true elsewhere inside the boundary",
         "[pcb][boardoutline]") {
    Document doc;
    const LayerId edgeCuts = doc.addLayer("Edge.Cuts", Color{0, 255, 0});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), edgeCuts, std::vector<Point2D>{{0, 0}, {40, 0}, {40, 30}, {0, 30}}, true));
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), edgeCuts, std::vector<Point2D>{{5, 5}, {10, 5}, {10, 10}, {5, 10}}, true));

    const BoardOutlineWithHoles outline = deriveBoardOutlineWithHoles(doc);
    REQUIRE(pointOnBoard(Point2D(20, 15), outline));  // plain board interior
    REQUIRE_FALSE(pointOnBoard(Point2D(7, 7), outline)); // inside the cutout
    REQUIRE_FALSE(pointOnBoard(Point2D(50, 50), outline)); // outside the boundary entirely
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

TEST_CASE("deriveKeepoutZones returns one zone per independent closed loop on the Keepout layer",
         "[pcb][boardoutline][keepout]") {
    Document doc;
    const LayerId keepout = doc.addLayer("Keepout", Color{255, 0, 255});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), keepout, std::vector<Point2D>{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true));
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), keepout, std::vector<Point2D>{{20, 0}, {26, 0}, {26, 4}, {20, 4}}, true));

    const auto zones = deriveKeepoutZones(doc);
    REQUIRE(zones.size() == 2); // unlike deriveBoardOutline, every loop becomes its own zone

    double totalArea = 0.0;
    for (const auto& zone : zones) {
        totalArea += polygonArea(zone.polygon);
        REQUIRE(zone.blocksCopperPour);
        REQUIRE(zone.blocksAutorouting);
        REQUIRE_FALSE(zone.layer.has_value());
    }
    REQUIRE(totalArea == Approx(10.0 * 10.0 + 6.0 * 4.0).margin(1e-6));
}

TEST_CASE("deriveKeepoutZones reads Keepout.NoPour/Keepout.NoRoute as pour-only/route-only restrictions",
         "[pcb][boardoutline][keepout]") {
    Document doc;
    const LayerId both = doc.addLayer("Keepout", Color{255, 0, 255});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), both, std::vector<Point2D>{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true));

    const LayerId pourOnly = doc.addLayer("Keepout.NoPour", Color{255, 0, 255});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), pourOnly, std::vector<Point2D>{{20, 0}, {30, 0}, {30, 10}, {20, 10}}, true));

    const LayerId routeOnly = doc.addLayer("Keepout.NoRoute", Color{255, 0, 255});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), routeOnly, std::vector<Point2D>{{40, 0}, {50, 0}, {50, 10}, {40, 10}}, true));

    const auto zones = deriveKeepoutZones(doc);
    REQUIRE(zones.size() == 3);

    int foundBoth = 0, foundPourOnly = 0, foundRouteOnly = 0;
    for (const auto& zone : zones) {
        if (zone.blocksCopperPour && zone.blocksAutorouting) ++foundBoth;
        else if (zone.blocksCopperPour && !zone.blocksAutorouting) ++foundPourOnly;
        else if (!zone.blocksCopperPour && zone.blocksAutorouting) ++foundRouteOnly;
        REQUIRE_FALSE(zone.layer.has_value()); // none of these 3 restrict to a single layer
    }
    REQUIRE(foundBoth == 1);
    REQUIRE(foundPourOnly == 1);
    REQUIRE(foundRouteOnly == 1);
}

TEST_CASE("deriveKeepoutZones reads Keepout.<LayerName> as a single-layer restriction",
         "[pcb][boardoutline][keepout]") {
    Document doc;
    const LayerId fCu = doc.addLayer("F.Cu", Color{200, 100, 0});
    const LayerId bCu = doc.addLayer("B.Cu", Color{0, 100, 200});

    const LayerId fCuKeepout = doc.addLayer("Keepout.F.Cu", Color{255, 0, 255});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), fCuKeepout, std::vector<Point2D>{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true));

    const auto zones = deriveKeepoutZones(doc);
    REQUIRE(zones.size() == 1);
    REQUIRE(zones[0].blocksCopperPour);
    REQUIRE(zones[0].blocksAutorouting);
    REQUIRE(zones[0].layer.has_value());
    REQUIRE(*zones[0].layer == fCu);

    REQUIRE(pointInKeepout(Point2D(5, 5), fCu, zones, /*forPour=*/true));
    REQUIRE_FALSE(pointInKeepout(Point2D(5, 5), bCu, zones, /*forPour=*/true)); // restricted to F.Cu only
}

TEST_CASE("deriveKeepoutZones ignores a Keepout.<suffix> layer whose suffix isn't a real layer name",
         "[pcb][boardoutline][keepout]") {
    Document doc;
    // "Widgets" isn't the name of any layer in the document -- not a
    // recognized keepout convention, so it's silently ignored rather
    // than misinterpreted.
    const LayerId unrecognized = doc.addLayer("Keepout.Widgets", Color{255, 0, 255});
    doc.addEntity(std::make_unique<PolylineEntity>(
        doc.reserveEntityId(), unrecognized, std::vector<Point2D>{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true));

    REQUIRE(deriveKeepoutZones(doc).empty());
}

TEST_CASE("deriveKeepoutZones chains a mixed line+arc loop the same way deriveBoardOutline does",
         "[pcb][boardoutline][keepout]") {
    Document doc;
    const LayerId keepout = doc.addLayer("Keepout", Color{255, 0, 255});
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), keepout, Point2D(0, 0), Point2D(10, 0)));
    doc.addEntity(std::make_unique<ArcEntity>(doc.reserveEntityId(), keepout, Point2D(10, 5), 5.0, -M_PI / 2, M_PI / 2));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), keepout, Point2D(10, 10), Point2D(0, 10)));
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), keepout, Point2D(0, 10), Point2D(0, 0)));

    const auto zones = deriveKeepoutZones(doc);
    REQUIRE(zones.size() == 1);
    const double expected = 10.0 * 10.0 + M_PI * 5.0 * 5.0 / 2.0; // rect + the semicircle bulge
    REQUIRE(polygonArea(zones[0].polygon) == Approx(expected).epsilon(1e-3));
}

TEST_CASE("deriveKeepoutZones returns empty when there's no Keepout layer or geometry",
         "[pcb][boardoutline][keepout]") {
    Document doc;
    REQUIRE(deriveKeepoutZones(doc).empty());

    doc.addLayer("Keepout", Color{255, 0, 255});
    REQUIRE(deriveKeepoutZones(doc).empty());
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
