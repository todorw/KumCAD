#include "core/document/Audit.h"
#include "core/document/Document.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"

#include <catch2/catch_test_macros.hpp>

using namespace lcad;

TEST_CASE("runAudit reports issues without changing the document when fix=false", "[audit]") {
    Document doc;
    const EntityId zeroLine = doc.reserveEntityId();
    doc.addEntity(std::make_unique<LineEntity>(zeroLine, 0, Point2D(5, 5), Point2D(5, 5)));

    const auto result = runAudit(doc, false);
    REQUIRE(result.issues.size() == 1);
    REQUIRE(result.issues[0].entityId == zeroLine);
    REQUIRE_FALSE(result.issues[0].fixed);
    REQUIRE(result.fixedCount == 0);
    REQUIRE(doc.findEntity(zeroLine) != nullptr); // untouched
}

TEST_CASE("runAudit with fix=true deletes degenerate geometry", "[audit]") {
    Document doc;
    const EntityId zeroLine = doc.reserveEntityId();
    doc.addEntity(std::make_unique<LineEntity>(zeroLine, 0, Point2D(5, 5), Point2D(5, 5)));
    const EntityId zeroCircle = doc.reserveEntityId();
    doc.addEntity(std::make_unique<CircleEntity>(zeroCircle, 0, Point2D(0, 0), 0.0));
    const EntityId shortPolyline = doc.reserveEntityId();
    doc.addEntity(std::make_unique<PolylineEntity>(shortPolyline, 0, std::vector<Point2D>{Point2D(0, 0)}));
    const EntityId goodLine = doc.reserveEntityId();
    doc.addEntity(std::make_unique<LineEntity>(goodLine, 0, Point2D(0, 0), Point2D(10, 10)));

    const auto result = runAudit(doc, true);
    REQUIRE(result.issues.size() == 3);
    REQUIRE(result.fixedCount == 3);
    for (const AuditIssue& issue : result.issues) REQUIRE(issue.fixed);

    REQUIRE(doc.findEntity(zeroLine) == nullptr);
    REQUIRE(doc.findEntity(zeroCircle) == nullptr);
    REQUIRE(doc.findEntity(shortPolyline) == nullptr);
    REQUIRE(doc.findEntity(goodLine) != nullptr); // real geometry survives
}

TEST_CASE("runAudit reassigns a dangling layer reference to layer 0 when fixing", "[audit]") {
    Document doc;
    const LayerId walls = doc.addLayer("Walls", Color{255, 0, 0});
    const EntityId id = doc.reserveEntityId();
    doc.addEntity(std::make_unique<LineEntity>(id, walls, Point2D(0, 0), Point2D(10, 0)));

    // Simulate corruption: the layer record is gone, but the entity still
    // references its old id (deleteLayer doesn't touch entities, matching
    // its own documented contract).
    doc.deleteLayer(walls);

    const auto reportOnly = runAudit(doc, false);
    REQUIRE(reportOnly.issues.size() == 1);
    REQUIRE(doc.findEntity(id)->layer() == walls); // still dangling

    const auto fixed = runAudit(doc, true);
    REQUIRE(fixed.fixedCount == 1);
    REQUIRE(doc.findEntity(id)->layer() == 0); // reassigned to layer 0
}

TEST_CASE("runAudit on a clean document reports nothing", "[audit]") {
    Document doc;
    doc.addEntity(std::make_unique<LineEntity>(doc.reserveEntityId(), 0, Point2D(0, 0), Point2D(10, 10)));
    doc.addEntity(std::make_unique<CircleEntity>(doc.reserveEntityId(), 0, Point2D(5, 5), 3.0));

    const auto result = runAudit(doc, true);
    REQUIRE(result.issues.empty());
    REQUIRE(result.fixedCount == 0);
}
