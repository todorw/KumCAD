#include "core/cam/GCodeWriter.h"
#include "core/cam/Toolpath.h"
#include "core/geometry/Polyline.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace lcad;
using Catch::Approx;

namespace {
struct TempPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_cam_test_" + std::to_string(std::rand()) + ".gcode");
    ~TempPath() { std::filesystem::remove(path); }
};

// A 20x20 square, CCW: (0,0) -> (20,0) -> (20,20) -> (0,20) -> close.
PolylineEntity makeSquare() {
    return PolylineEntity(1, 0, std::vector<Point2D>{Point2D(0, 0), Point2D(20, 0), Point2D(20, 20), Point2D(0, 20)},
                          true);
}
} // namespace

TEST_CASE("computeToolpath returns the profile unchanged for OnLine", "[cam][toolpath]") {
    const PolylineEntity square = makeSquare();
    ToolpathParams params;
    params.side = CutSide::OnLine;
    params.toolDiameter = 3.0;

    const auto path = computeToolpath(square, params);
    REQUIRE(path.size() == 4);
    REQUIRE(path[0].x == Approx(0.0));
}

TEST_CASE("computeToolpath offsets outward for Outside and inward for Inside", "[cam][toolpath]") {
    const PolylineEntity square = makeSquare();
    ToolpathParams params;
    params.toolDiameter = 4.0; // radius 2

    params.side = CutSide::Outside;
    const auto outside = computeToolpath(square, params);
    REQUIRE(outside.size() == 4);
    // Every outside vertex should be farther from the square's center (10,10)
    // than the original profile's corresponding corner distance (~14.14).
    for (const Point2D& p : outside) REQUIRE(p.distanceTo(Point2D(10, 10)) > Point2D(0, 0).distanceTo(Point2D(10, 10)));

    params.side = CutSide::Inside;
    const auto inside = computeToolpath(square, params);
    REQUIRE(inside.size() == 4);
    for (const Point2D& p : inside) REQUIRE(p.distanceTo(Point2D(10, 10)) < Point2D(0, 0).distanceTo(Point2D(10, 10)));
}

TEST_CASE("writeGCode emits a well-formed program with plunge, feed moves, and end", "[cam][gcode]") {
    TempPath temp;
    const std::vector<Point2D> path = {Point2D(0, 0), Point2D(10, 0), Point2D(10, 10), Point2D(0, 10)};
    ToolpathParams params;
    params.feedRate = 800.0;
    params.cutDepth = 2.0;

    REQUIRE(writeGCode(path, params, temp.path.string()));

    std::ifstream in(temp.path);
    std::ostringstream oss;
    oss << in.rdbuf();
    const std::string text = oss.str();

    REQUIRE(text.find("G21") != std::string::npos);
    REQUIRE(text.find("G90") != std::string::npos);
    REQUIRE(text.find("Z-2") != std::string::npos); // plunge to -cutDepth
    REQUIRE(text.find("F800") != std::string::npos);
    REQUIRE(text.find("M30") != std::string::npos);
}

TEST_CASE("writeGCode steps down in multiple passes when stepDown is set", "[cam][gcode]") {
    TempPath temp;
    const std::vector<Point2D> path = {Point2D(0, 0), Point2D(10, 0), Point2D(10, 10), Point2D(0, 10)};
    ToolpathParams params;
    params.cutDepth = 9.0;
    params.stepDown = 4.0;

    REQUIRE(writeGCode(path, params, temp.path.string()));

    std::ifstream in(temp.path);
    std::ostringstream oss;
    oss << in.rdbuf();
    const std::string text = oss.str();

    // ceil(9/4) == 3 passes at depths 4, 8, then the final pass clamped to 9.
    REQUIRE(text.find("Z-4") != std::string::npos);
    REQUIRE(text.find("Z-8") != std::string::npos);
    REQUIRE(text.find("Z-9") != std::string::npos);
    // Three plunges means the full XY loop is retraced three times: 3 feed moves/pass * 3 passes.
    std::size_t count = 0, pos = 0;
    while ((pos = text.find("G1 X", pos)) != std::string::npos) {
        ++count;
        pos += 4;
    }
    REQUIRE(count == 9);
}

TEST_CASE("writeGCode treats a stepDown >= cutDepth as a single pass", "[cam][gcode]") {
    TempPath temp;
    const std::vector<Point2D> path = {Point2D(0, 0), Point2D(10, 0), Point2D(10, 10), Point2D(0, 10)};
    ToolpathParams params;
    params.cutDepth = 3.0;
    params.stepDown = 5.0;

    REQUIRE(writeGCode(path, params, temp.path.string()));

    std::ifstream in(temp.path);
    std::ostringstream oss;
    oss << in.rdbuf();
    const std::string text = oss.str();

    std::size_t count = 0, pos = 0;
    while ((pos = text.find("G1 Z", pos)) != std::string::npos) {
        ++count;
        pos += 4;
    }
    REQUIRE(count == 1); // exactly one plunge
    REQUIRE(text.find("Z-3") != std::string::npos);
}

TEST_CASE("writeGCode rejects a degenerate toolpath", "[cam][gcode]") {
    TempPath temp;
    std::string error;
    REQUIRE_FALSE(writeGCode({Point2D(0, 0)}, ToolpathParams{}, temp.path.string(), &error));
    REQUIRE_FALSE(error.empty());
}
