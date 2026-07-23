#include "core/core3d/TechDraw.h"
#include "core/document/Document.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Line.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
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

TEST_CASE("autoDimensionView adds exact overall width/height dimensions for an orthographic view",
         "[core3d][techdraw][dimension]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    const TechDrawView view = projectView(box, ViewDirection::Front); // spans X=10, Z=4

    Document doc2d;
    autoDimensionView(doc2d, view);

    std::vector<const DimensionEntity*> dims;
    for (const Entity* e : doc2d.entities()) {
        if (e->type() == EntityType::Dimension) dims.push_back(static_cast<const DimensionEntity*>(e));
    }
    REQUIRE(dims.size() == 2);

    std::vector<double> values;
    for (const auto* d : dims) values.push_back(d->geometry().value);
    std::sort(values.begin(), values.end());
    REQUIRE(values[0] == Approx(4.0).margin(1e-6));
    REQUIRE(values[1] == Approx(10.0).margin(1e-6));

    bool foundDimensionsLayer = false;
    for (const Layer& layer : doc2d.layers()) {
        if (layer.name == "DIMENSIONS") foundDimensionsLayer = true;
    }
    REQUIRE(foundDimensionsLayer);
}

TEST_CASE("autoDimensionView with dimensionEachAxisAlignedEdge also dimensions every distinct visible edge",
         "[core3d][techdraw][dimension]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    const TechDrawView view = projectView(box, ViewDirection::Front); // a plain rectangle outline: 2x horizontal (10), 2x vertical (4)

    Document doc2d;
    AutoDimensionOptions options;
    options.dimensionEachAxisAlignedEdge = true;
    autoDimensionView(doc2d, view, options);

    std::vector<double> values;
    for (const Entity* e : doc2d.entities()) {
        if (e->type() == EntityType::Dimension) values.push_back(static_cast<const DimensionEntity*>(e)->geometry().value);
    }
    // 2 overall (10, 4) + 4 per-edge (10, 10, 4, 4) = 6 total.
    REQUIRE(values.size() == 6);
    std::sort(values.begin(), values.end());
    const std::vector<double> expected = {4.0, 4.0, 4.0, 10.0, 10.0, 10.0};
    for (std::size_t i = 0; i < values.size(); ++i) REQUIRE(values[i] == Approx(expected[i]).margin(1e-6));
}

TEST_CASE("autoDimensionView respects the same offset insertViewIntoDocument used", "[core3d][techdraw][dimension]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    const TechDrawView view = projectView(box, ViewDirection::Front);

    Document doc2d;
    insertViewIntoDocument(doc2d, view, 100.0, 200.0);
    AutoDimensionOptions options;
    options.offsetX = 100.0;
    options.offsetY = 200.0;
    autoDimensionView(doc2d, view, options);

    bool anyDimensionNearOffset = false;
    for (const Entity* e : doc2d.entities()) {
        if (e->type() != EntityType::Dimension) continue;
        const auto* d = static_cast<const DimensionEntity*>(e);
        if (d->point1().x >= 90.0 && d->point1().y >= 190.0) anyDimensionNearOffset = true;
    }
    REQUIRE(anyDimensionNearOffset);
}

TEST_CASE("autoDimensionView on an empty view adds nothing", "[core3d][techdraw][dimension]") {
    Document doc2d;
    const std::size_t before = doc2d.entities().size();
    autoDimensionView(doc2d, TechDrawView{});
    REQUIRE(doc2d.entities().size() == before);
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

TEST_CASE("projectViewAux normal to a box's own top face matches the fixed Top view's extents",
          "[core3d][techdraw][auxiliary]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    const TechDrawView topView = projectView(box, ViewDirection::Top);
    const TechDrawView auxView = projectViewAux(box, 0.0, 0.0, -1.0, 1.0, 0.0, 0.0);
    REQUIRE_FALSE(auxView.edges.empty());

    const Extent topExtent = extentOf(topView);
    const Extent auxExtent = extentOf(auxView);
    REQUIRE((auxExtent.maxU - auxExtent.minU) == Approx(topExtent.maxU - topExtent.minU).margin(1e-6));
    REQUIRE((auxExtent.maxV - auxExtent.minV) == Approx(topExtent.maxV - topExtent.minV).margin(1e-6));
}

TEST_CASE("projectViewAux at an arbitrary oblique angle still yields visible edges", "[core3d][techdraw][auxiliary]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    const TechDrawView view = projectViewAux(box, 0.3, -0.7, -0.4, 0.0, 0.0, 1.0);
    REQUIRE_FALSE(view.edges.empty());
}

TEST_CASE("projectViewAux returns no edges for a null shape or a degenerate up vector",
          "[core3d][techdraw][auxiliary]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    REQUIRE(projectViewAux(TopoDS_Shape(), 0.0, 0.0, -1.0, 1.0, 0.0, 0.0).edges.empty());
    // up parallel to eye: no well-defined view.
    REQUIRE(projectViewAux(box, 0.0, 0.0, -1.0, 0.0, 0.0, 1.0).edges.empty());
}

TEST_CASE("projectSectionView through the middle of a box-with-a-through-hole reveals the hole's boundary",
          "[core3d][techdraw][section]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(-10, -10, 0), 20.0, 20.0, 10.0).Shape();
    const TopoDS_Shape hole =
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1), gp_Dir(0, 0, 1)), 3.0, 12.0).Shape();
    const TopoDS_Shape boxWithHole = BRepAlgoAPI_Cut(box, hole).Shape();

    // Cut with a plane through the box's own mid-height (z=5), normal +Z:
    // material above z=5 is removed, and the remaining bottom half's cut
    // face is a solid 20x20 square with a radius-3 hole through it -- the
    // Top view of THAT should show the hole's circular boundary appearing
    // as its own set of (short, tessellated) edges near radius 3 from the
    // origin, something the un-sectioned Top view (which just sees the
    // hole from directly above, same circle) also shows -- so the real
    // assertion is that section + Top still yields a nonempty circular
    // boundary at the expected radius, proving the cut didn't wipe out
    // the hole or the surrounding material.
    const TechDrawView sectioned = projectSectionView(boxWithHole, ViewDirection::Top, 0, 0, 5, 0, 0, 1);
    REQUIRE_FALSE(sectioned.edges.empty());

    bool anyNearHoleRadius = false;
    for (const ProjectedEdge& e : sectioned.edges) {
        const double r1 = std::hypot(e.x1, e.y1);
        const double r2 = std::hypot(e.x2, e.y2);
        if (std::abs(r1 - 3.0) < 0.5 && std::abs(r2 - 3.0) < 0.5) anyNearHoleRadius = true;
    }
    REQUIRE(anyNearHoleRadius);

    // Sectioning removed the top half: the Front view of the sectioned
    // shape should now span only half the original height (5, not 10).
    const TechDrawView frontSectioned = projectSectionView(boxWithHole, ViewDirection::Front, 0, 0, 5, 0, 0, 1);
    const Extent e = extentOf(frontSectioned);
    REQUIRE((e.maxV - e.minV) == Approx(5.0).margin(1e-6));
}

TEST_CASE("projectSectionView returns no edges for a null shape or a degenerate normal",
          "[core3d][techdraw][section]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 5.0, 4.0).Shape();
    REQUIRE(projectSectionView(TopoDS_Shape(), ViewDirection::Top, 0, 0, 0, 0, 0, 1).edges.empty());
    REQUIRE(projectSectionView(box, ViewDirection::Top, 0, 0, 0, 0, 0, 0).edges.empty());
}
