#include "core/core3d/Cam3D.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace lcad;
using Catch::Approx;

namespace {

struct TempPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_cam3d_test_" + std::to_string(std::rand()) + ".gcode");
    ~TempPath() { std::filesystem::remove(path); }
};

struct Extent {
    double minX, maxX, minY, maxY;
};

Extent extentOf(const std::vector<Point2D>& pts) {
    Extent e{std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest(),
             std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
    for (const Point2D& p : pts) {
        e.minX = std::min(e.minX, p.x);
        e.maxX = std::max(e.maxX, p.x);
        e.minY = std::min(e.minY, p.y);
        e.maxY = std::max(e.maxY, p.y);
    }
    return e;
}

} // namespace

TEST_CASE("sliceIntoLevels of a plain box produces one level per stepDown plus a final bottom pass",
          "[core3d][cam3d]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(50.0, 50.0, 20.0).Shape();
    Cam3DParams params;
    params.stepDown = 5.0;
    params.side = CutSide::OnLine;

    const std::vector<Cam3DLevel> levels = sliceIntoLevels(box, params);
    // z = 15, 10, 5, then a final epsilon-above-bottom pass -- 4 levels.
    REQUIRE(levels.size() == 4);
    REQUIRE(levels[0].z == Approx(15.0));
    REQUIRE(levels[1].z == Approx(10.0));
    REQUIRE(levels[2].z == Approx(5.0));
    REQUIRE(levels[3].z > 0.0);
    REQUIRE(levels[3].z < 5.0);
}

TEST_CASE("sliceIntoLevels' OnLine toolpath at each level matches the box's own footprint", "[core3d][cam3d]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(50.0, 30.0, 20.0).Shape();
    Cam3DParams params;
    params.stepDown = 10.0;
    params.side = CutSide::OnLine;

    const std::vector<Cam3DLevel> levels = sliceIntoLevels(box, params);
    REQUIRE_FALSE(levels.empty());
    for (const Cam3DLevel& level : levels) {
        const Extent e = extentOf(level.toolpath);
        REQUIRE((e.maxX - e.minX) == Approx(50.0).margin(1e-6));
        REQUIRE((e.maxY - e.minY) == Approx(30.0).margin(1e-6));
    }
}

TEST_CASE("sliceIntoLevels' Outside toolpath is offset outward by the tool radius", "[core3d][cam3d]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(50.0, 30.0, 20.0).Shape();
    Cam3DParams params;
    params.stepDown = 10.0;
    params.side = CutSide::Outside;
    params.toolDiameter = 6.0;

    const std::vector<Cam3DLevel> levels = sliceIntoLevels(box, params);
    REQUIRE_FALSE(levels.empty());
    const Extent e = extentOf(levels[0].toolpath);
    REQUIRE((e.maxX - e.minX) == Approx(50.0 + 6.0).margin(1e-3));
    REQUIRE((e.maxY - e.minY) == Approx(30.0 + 6.0).margin(1e-3));
}

TEST_CASE("sliceIntoLevels picks the largest loop, dropping a pocket's inner boundary at that level",
          "[core3d][cam3d]") {
    // A box with a blind cylindrical pocket in the middle: a slice through
    // the pocket depth produces two loops (outer box + inner pocket
    // circle) -- only the outer (larger) one should survive.
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(50.0, 50.0, 20.0).Shape();
    const TopoDS_Shape pocket = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(25, 25, 5), gp_Dir(0, 0, 1)), 8.0, 20.0).Shape();
    BRepAlgoAPI_Cut cut(box, pocket);
    REQUIRE(cut.IsDone());

    Cam3DParams params;
    params.stepDown = 5.0;
    params.side = CutSide::OnLine;
    const std::vector<Cam3DLevel> levels = sliceIntoLevels(cut.Shape(), params);
    REQUIRE_FALSE(levels.empty());

    // A slice at z=10 (well within the pocket's depth) should still report
    // the box's own 50x50 footprint, not the small 16-diameter pocket loop.
    for (const Cam3DLevel& level : levels) {
        if (std::abs(level.z - 10.0) > 1.0) continue;
        const Extent e = extentOf(level.toolpath);
        REQUIRE((e.maxX - e.minX) == Approx(50.0).margin(1e-6));
    }
}

TEST_CASE("sliceIntoLevels rejects a null shape or non-positive stepDown", "[core3d][cam3d]") {
    Cam3DParams params;
    REQUIRE(sliceIntoLevels(TopoDS_Shape(), params).empty());

    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    Cam3DParams badParams;
    badParams.stepDown = 0.0;
    REQUIRE(sliceIntoLevels(box, badParams).empty());
}

TEST_CASE("writeMultiLevelGCode writes a file with one plunge per level and an M30 end", "[core3d][cam3d]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 20.0, 10.0).Shape();
    Cam3DParams params;
    params.stepDown = 5.0;
    const std::vector<Cam3DLevel> levels = sliceIntoLevels(box, params);
    REQUIRE_FALSE(levels.empty());

    TempPath temp;
    std::string error;
    REQUIRE(writeMultiLevelGCode(levels, params, temp.path.string(), &error));

    std::ifstream in(temp.path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(content.find("G21") != std::string::npos);
    REQUIRE(content.find("M30") != std::string::npos);

    std::size_t plungeCount = 0;
    std::size_t pos = 0;
    while ((pos = content.find("G1 Z", pos)) != std::string::npos) {
        ++plungeCount;
        pos += 1;
    }
    REQUIRE(plungeCount == levels.size());
}

TEST_CASE("writeMultiLevelGCode fails cleanly on an empty level list", "[core3d][cam3d]") {
    std::string error;
    REQUIRE_FALSE(writeMultiLevelGCode({}, Cam3DParams{}, "/tmp/kumcad_cam3d_unused.gcode", &error));
    REQUIRE_FALSE(error.empty());
}
