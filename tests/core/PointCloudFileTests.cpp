#include "core/io/PointCloudFile.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using Catch::Approx;

namespace {

struct TempXyzPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_xyz_test_" + std::to_string(std::rand()) + ".xyz");
    ~TempXyzPath() { std::filesystem::remove(path); }
};

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

} // namespace

TEST_CASE("readPointCloudXyz parses whitespace and comma dialects, skips comments", "[pointcloud]") {
    TempXyzPath temp;
    writeFile(temp.path, "# header comment\n1.0 2.0 3.0\n4.0,5.0,6.0\n\n7.5 8.5\n");

    const auto points = lcad::readPointCloudXyz(temp.path.string());
    REQUIRE(points.size() == 3);
    REQUIRE(points[0].x == Approx(1.0));
    REQUIRE(points[0].y == Approx(2.0));
    REQUIRE(points[1].x == Approx(4.0));
    REQUIRE(points[1].y == Approx(5.0));
    REQUIRE(points[2].x == Approx(7.5));
    REQUIRE(points[2].y == Approx(8.5));
}

TEST_CASE("readPointCloudXyz decimates evenly when over the cap", "[pointcloud]") {
    TempXyzPath temp;
    std::string content;
    for (int i = 0; i < 1000; ++i) content += std::to_string(i) + " 0 0\n";
    writeFile(temp.path, content);

    const auto points = lcad::readPointCloudXyz(temp.path.string(), 100);
    REQUIRE(points.size() <= 101);
    REQUIRE(points.size() >= 90);
    // Decimation keeps the first point and preserves order.
    REQUIRE(points.front().x == Approx(0.0));
    REQUIRE(points[1].x > points[0].x);
}

TEST_CASE("readPointCloudXyz returns empty for a missing or unparsable file", "[pointcloud]") {
    REQUIRE(lcad::readPointCloudXyz("/no/such/file.xyz").empty());

    TempXyzPath temp;
    writeFile(temp.path, "not a number\nalso not\n");
    REQUIRE(lcad::readPointCloudXyz(temp.path.string()).empty());
}
