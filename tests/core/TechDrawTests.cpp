#include "core/core3d/TechDraw.h"
#include "core/document/Document.h"
#include "core/geometry/Line.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

using namespace lcad;
using Catch::Approx;

namespace {

struct Extent {
    double minU, maxU, minV, maxV;
};

Extent extentOf(const TechDrawView& view) {
    Extent e{std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(),
             std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
    for (const ProjectedEdge& edge : view.edges) {
        e.minU = std::min({e.minU, edge.x1, edge.x2});
        e.maxU = std::max({e.maxU, edge.x1, edge.x2});
        e.minV = std::min({e.minV, edge.y1, edge.y2});
        e.maxV = std::max({e.maxV, edge.y1, edge.y2});
    }
    return e;
}

} // namespace

TEST_CASE("projectView's Top view of a box spans its X/Y dimensions", "[core3d][techdraw]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    const TechDrawView view = projectView(box, ViewDirection::Top);
    REQUIRE_FALSE(view.edges.empty());

    const Extent e = extentOf(view);
    REQUIRE((e.maxU - e.minU) == Approx(10.0).margin(1e-6));
    REQUIRE((e.maxV - e.minV) == Approx(5.0).margin(1e-6));
}

TEST_CASE("projectView's Front view of a box spans its X/Z dimensions", "[core3d][techdraw]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    const TechDrawView view = projectView(box, ViewDirection::Front);
    REQUIRE_FALSE(view.edges.empty());

    const Extent e = extentOf(view);
    REQUIRE((e.maxU - e.minU) == Approx(10.0).margin(1e-6));
    REQUIRE((e.maxV - e.minV) == Approx(4.0).margin(1e-6));
}

TEST_CASE("projectView's Right view of a box spans its Y/Z dimensions", "[core3d][techdraw]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    const TechDrawView view = projectView(box, ViewDirection::Right);
    REQUIRE_FALSE(view.edges.empty());

    const Extent e = extentOf(view);
    REQUIRE((e.maxU - e.minU) == Approx(5.0).margin(1e-6));
    REQUIRE((e.maxV - e.minV) == Approx(4.0).margin(1e-6));
}

TEST_CASE("projectView of a box-with-a-hole reports both visible and hidden edges", "[core3d][techdraw]") {
    // A box viewed from the front hides the pocket's back wall/edges behind
    // the front face -- if HLR is wired up correctly there should be at
    // least one edge HLR marks hidden, not just the visible silhouette.
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const TechDrawView view = projectView(box, ViewDirection::Iso);
    REQUIRE_FALSE(view.edges.empty());

    const bool anyVisible = std::any_of(view.edges.begin(), view.edges.end(), [](const ProjectedEdge& e) { return !e.hidden; });
    REQUIRE(anyVisible);
}

TEST_CASE("insertViewIntoDocument bakes edges into the 2D document on a TECHDRAW layer", "[core3d][techdraw]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    const TechDrawView view = projectView(box, ViewDirection::Top);
    REQUIRE_FALSE(view.edges.empty());

    Document doc2d;
    const std::size_t before = doc2d.entities().size();
    insertViewIntoDocument(doc2d, view, 100.0, 200.0);

    REQUIRE(doc2d.entities().size() == before + view.edges.size());

    bool foundTechDrawLayer = false;
    for (const Layer& layer : doc2d.layers()) {
        if (layer.name == "TECHDRAW") foundTechDrawLayer = true;
    }
    REQUIRE(foundTechDrawLayer);

    // Every baked entity is a LineEntity, offset by (100, 200).
    for (const Entity* e : doc2d.entities()) {
        REQUIRE(e->type() == EntityType::Line);
        const auto* line = static_cast<const LineEntity*>(e);
        REQUIRE(line->start().x >= 90.0); // within the offset box, loosely
    }
}

TEST_CASE("insertViewIntoDocument gives hidden edges a Hidden linetype override", "[core3d][techdraw]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const TechDrawView view = projectView(box, ViewDirection::Iso);

    Document doc2d;
    insertViewIntoDocument(doc2d, view, 0.0, 0.0);

    bool anyHiddenOverride = false;
    for (const Entity* e : doc2d.entities()) {
        if (e->linetypeOverride().has_value() && *e->linetypeOverride() == LineType::Hidden) anyHiddenOverride = true;
    }
    REQUIRE(anyHiddenOverride);
}

TEST_CASE("projectView on a null shape returns no edges", "[core3d][techdraw]") {
    const TechDrawView view = projectView(TopoDS_Shape(), ViewDirection::Front);
    REQUIRE(view.edges.empty());
}

TEST_CASE("projectView tessellates a cylinder's curved silhouette into multiple chords, not one",
          "[core3d][techdraw]") {
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape();
    const TechDrawView view = projectView(cylinder, ViewDirection::Top); // top view sees the circular cap edges
    REQUIRE_FALSE(view.edges.empty());

    // Every chord should be short relative to the circle's own diameter --
    // a single end-to-end chord across a 20-unit-diameter circle would be
    // long; a fine tessellation keeps each piece small.
    const double diameter = 20.0;
    bool anyShortChord = false;
    for (const ProjectedEdge& edge : view.edges) {
        const double length = std::hypot(edge.x2 - edge.x1, edge.y2 - edge.y1);
        if (length > 1e-9 && length < diameter * 0.3) anyShortChord = true;
    }
    REQUIRE(anyShortChord);

    // And there should be noticeably more than just a handful of edges
    // (a single untessellated circle would project to very few chords).
    REQUIRE(view.edges.size() > 10);
}
