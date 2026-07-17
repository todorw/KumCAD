#include "core/document/Document.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Via.h"
#include "core/pcb/CopperPour.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace lcad;
using Catch::Approx;

namespace {
double totalHatchArea(const Document& doc) {
    double area = 0.0;
    for (const Entity* e : doc.entities()) {
        if (e->type() != EntityType::Hatch) continue;
        const auto* hatch = static_cast<const HatchEntity*>(e);
        const auto& v = hatch->vertices();
        // Every piece here is an axis-aligned rectangle (see CopperPour.cpp).
        area += std::abs(v[2].x - v[0].x) * std::abs(v[2].y - v[0].y);
    }
    return area;
}
} // namespace

TEST_CASE("buildCopperPourWithClearance fills an empty rectangular boundary completely", "[pcb][pour]") {
    Document doc;
    const LayerId layer = doc.addLayer("F.Cu", Color{200, 100, 0});
    const std::vector<Point2D> boundary = {{0, 0}, {20, 0}, {20, 10}, {0, 10}};

    const auto ids = buildCopperPourWithClearance(doc, layer, boundary, {}, 1.0, 0.2);
    REQUIRE_FALSE(ids.empty());
    // A 1.0 grid over a 20x10 boundary tiles it exactly (no partial cells).
    REQUIRE(totalHatchArea(doc) == Approx(20.0 * 10.0).margin(1e-6));
}

TEST_CASE("buildCopperPourWithClearance leaves a gap around a non-exempt via", "[pcb][pour]") {
    Document doc;
    const LayerId layer = doc.addLayer("F.Cu", Color{200, 100, 0});
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), layer, Point2D(10, 5), 1.0, 0.5));

    const std::vector<Point2D> boundary = {{0, 0}, {20, 0}, {20, 10}, {0, 10}};
    const auto ids = buildCopperPourWithClearance(doc, layer, boundary, {}, 0.5, 0.5);
    REQUIRE_FALSE(ids.empty());
    // The via (radius 0.5) plus 0.5 clearance excludes at least a 1-unit-
    // radius disc around (10,5) -- strictly less area than the full boundary.
    REQUIRE(totalHatchArea(doc) < 20.0 * 10.0);
}

TEST_CASE("buildCopperPourWithClearance does not exclude cells near an exempt (own-net) position",
          "[pcb][pour]") {
    Document doc;
    const LayerId layer = doc.addLayer("F.Cu", Color{200, 100, 0});
    // A via at the same spot, but this time it's the pour's OWN net --
    // should not carve out a clearance gap around it.
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), layer, Point2D(10, 5), 1.0, 0.5));

    const std::vector<Point2D> boundary = {{0, 0}, {20, 0}, {20, 10}, {0, 10}};
    const auto ids = buildCopperPourWithClearance(doc, layer, boundary, {Point2D(10, 5)}, 0.5, 0.5);
    REQUIRE_FALSE(ids.empty());
    REQUIRE(totalHatchArea(doc) == Approx(20.0 * 10.0).margin(1e-6));
}

TEST_CASE("buildCopperPourWithClearance rejects degenerate input", "[pcb][pour]") {
    Document doc;
    const LayerId layer = doc.addLayer("F.Cu", Color{200, 100, 0});

    REQUIRE(buildCopperPourWithClearance(doc, layer, {{0, 0}, {1, 1}}, {}, 0.5, 0.2).empty()); // <3 boundary points
    REQUIRE(buildCopperPourWithClearance(doc, layer, {{0, 0}, {10, 0}, {10, 10}}, {}, 0.0, 0.2).empty()); // gridSize <= 0
}

TEST_CASE("thermal relief carves a keepout ring around an own-net pad, crossed by spokes", "[pcb][pour][thermal]") {
    Document doc;
    const LayerId layer = doc.addLayer("F.Cu", Color{200, 100, 0});
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), layer, Point2D(10, 5), 1.0, 0.5));

    const std::vector<Point2D> boundary = {{0, 0}, {20, 0}, {20, 10}, {0, 10}};
    ThermalReliefParams relief;
    relief.enabled = true;
    relief.antipadRadius = 1.5;
    relief.spokeWidth = 0.3;
    relief.spokeCount = 4;

    const auto ids = buildCopperPourWithClearance(doc, layer, boundary, {Point2D(10, 5)}, 0.1, 0.2, relief);
    REQUIRE_FALSE(ids.empty());
    // Solid own-net connection (default) fills the whole boundary; thermal
    // relief must carve SOME of it away even though the via is own-net.
    REQUIRE(totalHatchArea(doc) < 20.0 * 10.0);

    // A point on the 0-degree spoke (straight +X from the via, well inside
    // the antipad ring) must be covered by SOME filled hatch rectangle; a
    // point at the same radius but off any spoke must not be covered by
    // any -- checked by bounding-box containment against each rectangle,
    // not by scanning for a nearby vertex (a merged run's own corners can
    // legitimately land well outside either probe point's neighborhood).
    auto coveredByAnyHatch = [&](const Point2D& p) {
        for (const Entity* e : doc.entities()) {
            if (e->type() != EntityType::Hatch) continue;
            const auto* hatch = static_cast<const HatchEntity*>(e);
            const auto& v = hatch->vertices();
            const double x1 = std::min(v[0].x, v[2].x), x2 = std::max(v[0].x, v[2].x);
            const double y1 = std::min(v[0].y, v[2].y), y2 = std::max(v[0].y, v[2].y);
            if (p.x >= x1 && p.x <= x2 && p.y >= y1 && p.y <= y2) return true;
        }
        return false;
    };

    REQUIRE(coveredByAnyHatch(Point2D(11.0, 5.0))); // dist=1.0 from via, exactly on the 0-degree spoke
    // ~52 degrees from the via (between the 0/90-degree spokes), same
    // radius: must be excluded entirely.
    const Point2D offSpoke(10.0 + 1.0 * std::cos(0.9), 5.0 + 1.0 * std::sin(0.9));
    REQUIRE_FALSE(coveredByAnyHatch(offSpoke));
}

TEST_CASE("thermal relief disabled (default) keeps the original solid own-net connection", "[pcb][pour][thermal]") {
    Document doc;
    const LayerId layer = doc.addLayer("F.Cu", Color{200, 100, 0});
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), layer, Point2D(10, 5), 1.0, 0.5));

    const std::vector<Point2D> boundary = {{0, 0}, {20, 0}, {20, 10}, {0, 10}};
    const auto ids = buildCopperPourWithClearance(doc, layer, boundary, {Point2D(10, 5)}, 0.5, 0.5); // ThermalReliefParams defaulted
    REQUIRE_FALSE(ids.empty());
    REQUIRE(totalHatchArea(doc) == Approx(20.0 * 10.0).margin(1e-6));
}
