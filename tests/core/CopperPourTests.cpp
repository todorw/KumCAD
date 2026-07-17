#include "core/document/Document.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Via.h"
#include "core/pcb/CopperPour.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
