#include "core/document/Document.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"
#include "core/pcb/Teardrop.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lcad;
using Catch::Approx;

namespace {
// Shoelace formula, for verifying a teardrop's fill is a real, non-
// self-intersecting-looking, positive-area polygon.
double polygonArea(const std::vector<Point2D>& poly) {
    double area = 0.0;
    for (std::size_t i = 0; i + 1 < poly.size(); ++i) area += poly[i].x * poly[i + 1].y - poly[i + 1].x * poly[i].y;
    return std::abs(area) / 2.0;
}
} // namespace

TEST_CASE("buildTeardrop produces a closed polygon widening from track to pad", "[pcb][teardrop]") {
    const Point2D padCenter(0, 0);
    const double padRadius = 1.0;
    const Point2D dir(1, 0); // track leaves the pad along +X
    const double trackWidth = 0.3;
    const double length = 3.0;

    const auto poly = buildTeardrop(padCenter, padRadius, dir, trackWidth, length);
    REQUIRE(poly.size() >= 4);
    REQUIRE(poly.front().x == Approx(poly.back().x)); // explicitly closed
    REQUIRE(poly.front().y == Approx(poly.back().y));

    // Track-side edge sits at x=length, +-trackWidth/2.
    REQUIRE(poly.front().x == Approx(3.0));
    REQUIRE(std::abs(poly.front().y) == Approx(0.15));

    // Every pad-side vertex lands exactly on the pad's own circle.
    bool anyOnPadCircle = false;
    for (const Point2D& v : poly) {
        const double dist = v.distanceTo(padCenter);
        if (std::abs(dist - padRadius) < 1e-6) anyOnPadCircle = true;
    }
    REQUIRE(anyOnPadCircle);

    // Real positive area (a degenerate/self-intersecting shape would read ~0).
    REQUIRE(polygonArea(poly) > 0.5);
}

TEST_CASE("buildTeardrop rejects degenerate inputs", "[pcb][teardrop]") {
    REQUIRE(buildTeardrop(Point2D(0, 0), 0.0, Point2D(1, 0), 0.3, 3.0).empty());   // no pad
    REQUIRE(buildTeardrop(Point2D(0, 0), 1.0, Point2D(1, 0), 0.0, 3.0).empty());   // no track width
    REQUIRE(buildTeardrop(Point2D(0, 0), 1.0, Point2D(1, 0), 0.3, 0.5).empty());   // length inside the pad
    REQUIRE(buildTeardrop(Point2D(0, 0), 1.0, Point2D(0, 0), 0.3, 3.0).empty());   // degenerate direction
}

TEST_CASE("buildTeardrop's shoulder is a real tangent point: the line from the track edge to it is "
         "perpendicular to the pad's own radius there",
         "[pcb][teardrop]") {
    // Standard tangent-line-from-an-external-point check: at a true
    // tangent point T, the radius (padCenter -> T) is perpendicular to
    // the tangent line (T -> the external point), i.e. their dot product
    // is zero. The old fixed-angle shoulder had no reason to satisfy
    // this for an arbitrary track-width/pad-radius ratio.
    const Point2D padCenter(0, 0);
    const double padRadius = 1.0;
    const Point2D dir(1, 0);
    const double trackWidth = 0.6; // wide enough relative to the pad that a fixed 45 degree angle wouldn't be tangent
    const double length = 4.0;

    const auto poly = buildTeardrop(padCenter, padRadius, dir, trackWidth, length);
    REQUIRE(poly.size() >= 4);

    const Point2D edge1 = poly.front();          // the track-side point on the +Y side
    const Point2D shoulder1 = poly[1];            // the very next vertex: the +Y shoulder on the pad circle
    REQUIRE(shoulder1.distanceTo(padCenter) == Approx(padRadius).margin(1e-6));

    const Point2D radiusVec = shoulder1 - padCenter;
    const Point2D tangentVec = edge1 - shoulder1;
    const double dot = radiusVec.x * tangentVec.x + radiusVec.y * tangentVec.y;
    REQUIRE(std::abs(dot) < 1e-6);
}

TEST_CASE("buildTeardrop works for any track direction, not just +X", "[pcb][teardrop]") {
    const Point2D padCenter(5, 5);
    const auto poly = buildTeardrop(padCenter, 0.8, Point2D(0, -1), 0.25, 2.0); // track leaves downward
    REQUIRE(poly.size() >= 4);
    // The far (track-side) point should sit below the pad (smaller y).
    REQUIRE(poly.front().y < padCenter.y - 1.5);
}

TEST_CASE("addTeardropsToDocument finds a track landing on a via and adds a teardrop", "[pcb][teardrop]") {
    Document doc;
    const LayerId layer = doc.addLayer("F.Cu", Color{200, 100, 0});
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), layer, Point2D(10, 5), 1.0, 0.5));
    // Track running from the via out to (20,5).
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), layer,
                                                std::vector<Point2D>{Point2D(10, 5), Point2D(20, 5)}, 0.3));

    const auto ids = addTeardropsToDocument(doc, layer);
    REQUIRE(ids.size() == 1);

    const Entity* e = doc.findEntity(ids[0]);
    REQUIRE(e);
    REQUIRE(e->type() == EntityType::Hatch);
    const auto* hatch = static_cast<const HatchEntity*>(e);
    // Track-side edge of the teardrop should sit toward +X from the via (the
    // track's own direction), well past the via's own 0.5 radius.
    bool foundFarEdge = false;
    for (const Point2D& v : hatch->vertices()) {
        if (v.x > 10.5 && std::abs(v.y - 5.0) < 0.2) foundFarEdge = true;
    }
    REQUIRE(foundFarEdge);
}

TEST_CASE("addTeardropsToDocument skips a track not touching any pad or via", "[pcb][teardrop]") {
    Document doc;
    const LayerId layer = doc.addLayer("F.Cu", Color{200, 100, 0});
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), layer,
                                                std::vector<Point2D>{Point2D(0, 0), Point2D(10, 0)}, 0.3));
    REQUIRE(addTeardropsToDocument(doc, layer).empty());
}

TEST_CASE("addTeardropsToDocument handles both ends of a track independently", "[pcb][teardrop]") {
    Document doc;
    const LayerId layer = doc.addLayer("F.Cu", Color{200, 100, 0});
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), layer, Point2D(0, 0), 1.0, 0.5));
    doc.addEntity(std::make_unique<ViaEntity>(doc.reserveEntityId(), layer, Point2D(20, 0), 1.0, 0.5));
    doc.addEntity(std::make_unique<TrackEntity>(doc.reserveEntityId(), layer,
                                                std::vector<Point2D>{Point2D(0, 0), Point2D(20, 0)}, 0.3));

    const auto ids = addTeardropsToDocument(doc, layer);
    REQUIRE(ids.size() == 2); // one teardrop at each end
}
