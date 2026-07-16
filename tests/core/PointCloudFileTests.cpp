#include "core/io/PointCloudFile.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

#ifdef LCAD_HAS_LASZIP
#define LASZIP_API_VERSION
#include <laszip/laszip_api.h>
#endif

using Catch::Approx;

namespace {

struct TempXyzPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_xyz_test_" + std::to_string(std::rand()) + ".xyz");
    ~TempXyzPath() { std::filesystem::remove(path); }
};

struct TempLasPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_las_test_" + std::to_string(std::rand()) + ".las");
    ~TempLasPath() { std::filesystem::remove(path); }
};

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

void putU16(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void putU32(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

void putI32(std::string& out, std::int32_t v) { putU32(out, static_cast<std::uint32_t>(v)); }

void putF64(std::string& out, double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((bits >> (8 * i)) & 0xFF));
}

// Builds a minimal, spec-accurate LAS 1.2 file: a 227-byte header followed
// by point-format-0 records (20 bytes each -- 12 for X/Y/Z, 8 padding for
// intensity/flags/classification/scan angle/user data/point source ID).
std::string buildLasFile(const std::vector<std::pair<std::int32_t, std::int32_t>>& rawXY, double scale = 0.01,
                          double offsetX = 0.0, double offsetY = 0.0) {
    std::string h;
    h += "LASF";                  // 0: signature
    putU16(h, 0);                 // 4: file source ID
    putU16(h, 0);                 // 6: global encoding
    putU32(h, 0);                 // 8: GUID data 1
    putU16(h, 0);                 // 12: GUID data 2
    putU16(h, 0);                 // 14: GUID data 3
    h.append(8, '\0');            // 16: GUID data 4
    h.push_back(1);               // 24: version major
    h.push_back(2);               // 25: version minor
    h.append(32, '\0');           // 26: system identifier
    h.append(32, '\0');           // 58: generating software
    putU16(h, 0);                 // 90: creation day
    putU16(h, 0);                 // 92: creation year
    putU16(h, 227);               // 94: header size
    putU32(h, 227);               // 96: offset to point data
    putU32(h, 0);                 // 100: number of VLRs
    h.push_back(0);               // 104: point data format 0
    putU16(h, 20);                // 105: point data record length
    putU32(h, static_cast<std::uint32_t>(rawXY.size())); // 107: number of point records
    for (int i = 0; i < 5; ++i) putU32(h, 0);            // 111: points by return
    putF64(h, scale);             // 131: X scale
    putF64(h, scale);             // 139: Y scale
    putF64(h, 1.0);               // 147: Z scale
    putF64(h, offsetX);           // 155: X offset
    putF64(h, offsetY);           // 163: Y offset
    putF64(h, 0.0);               // 171: Z offset
    for (int i = 0; i < 6; ++i) putF64(h, 0.0); // 179: max/min X/Y/Z (unused by the reader)
    REQUIRE(h.size() == 227);

    for (const auto& [rx, ry] : rawXY) {
        putI32(h, rx);
        putI32(h, ry);
        putI32(h, 0); // Z
        h.append(8, '\0'); // intensity/flags/classification/scan angle/user data/point source ID
    }
    return h;
}

#ifdef LCAD_HAS_LASZIP
struct TempLazPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_laz_test_" + std::to_string(std::rand()) + ".laz");
    ~TempLazPath() { std::filesystem::remove(path); }
};

// Writes a genuinely LZ-compressed LAZ file via LASzip itself (rather than a
// checked-in binary fixture), so the round trip through readPointCloudLaz
// exercises real compression/decompression, not just a header parse.
void writeLazFile(const std::filesystem::path& path,
                   const std::vector<std::pair<std::int32_t, std::int32_t>>& rawXY, double scale = 0.01,
                   double offsetX = 0.0, double offsetY = 0.0) {
    static std::once_flag loaded;
    std::call_once(loaded, [] { laszip_load_dll(); });

    laszip_POINTER writer = nullptr;
    REQUIRE(laszip_create(&writer) == 0);

    laszip_header_struct header{};
    header.version_major = 1;
    header.version_minor = 2;
    header.header_size = 227;
    header.offset_to_point_data = 227;
    header.point_data_format = 0;
    header.point_data_record_length = 20;
    header.number_of_point_records = static_cast<laszip_U32>(rawXY.size());
    header.x_scale_factor = scale;
    header.y_scale_factor = scale;
    header.z_scale_factor = 1.0;
    header.x_offset = offsetX;
    header.y_offset = offsetY;
    REQUIRE(laszip_set_header(writer, &header) == 0);

    laszip_point_struct* point = nullptr;
    REQUIRE(laszip_get_point_pointer(writer, &point) == 0);
    REQUIRE(laszip_open_writer(writer, path.string().c_str(), 1) == 0);

    for (const auto& [rx, ry] : rawXY) {
        point->X = rx;
        point->Y = ry;
        point->Z = 0;
        REQUIRE(laszip_write_point(writer) == 0);
    }

    REQUIRE(laszip_close_writer(writer) == 0);
    REQUIRE(laszip_destroy(writer) == 0);
}
#endif

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

TEST_CASE("readPointCloudLas descales X/Y through the header's scale and offset", "[pointcloud][las]") {
    TempLasPath temp;
    // Raw integers 100/200 with scale 0.01 and offset 5.0/-3.0 -> (6.0, -1.0).
    const std::string las = buildLasFile({{100, 200}, {300, -400}}, 0.01, 5.0, -3.0);
    writeFile(temp.path, las);

    const auto points = lcad::readPointCloudLas(temp.path.string());
    REQUIRE(points.size() == 2);
    REQUIRE(points[0].x == Approx(6.0));
    REQUIRE(points[0].y == Approx(-1.0));
    REQUIRE(points[1].x == Approx(8.0));
    REQUIRE(points[1].y == Approx(-7.0));
}

TEST_CASE("readPointCloudLas decimates evenly when over the cap", "[pointcloud][las]") {
    TempLasPath temp;
    std::vector<std::pair<std::int32_t, std::int32_t>> raw;
    for (int i = 0; i < 1000; ++i) raw.emplace_back(i, 0);
    writeFile(temp.path, buildLasFile(raw));

    const auto points = lcad::readPointCloudLas(temp.path.string(), 100);
    REQUIRE(points.size() <= 101);
    REQUIRE(points.size() >= 90);
    REQUIRE(points.front().x == Approx(0.0));
    REQUIRE(points[1].x > points[0].x);
}

TEST_CASE("readPointCloudLas rejects files without a valid LASF signature", "[pointcloud][las]") {
    REQUIRE(lcad::readPointCloudLas("/no/such/file.las").empty());

    TempLasPath temp;
    writeFile(temp.path, "not a las file, way too short");
    REQUIRE(lcad::readPointCloudLas(temp.path.string()).empty());
}

TEST_CASE("readPointCloudFile dispatches by extension", "[pointcloud]") {
    TempLasPath lasTemp;
    writeFile(lasTemp.path, buildLasFile({{100, 200}}, 0.01, 0.0, 0.0));
    const auto lasPoints = lcad::readPointCloudFile(lasTemp.path.string());
    REQUIRE(lasPoints.size() == 1);
    REQUIRE(lasPoints[0].x == Approx(1.0));

    TempXyzPath xyzTemp;
    writeFile(xyzTemp.path, "1.0 2.0 3.0\n");
    const auto xyzPoints = lcad::readPointCloudFile(xyzTemp.path.string());
    REQUIRE(xyzPoints.size() == 1);
    REQUIRE(xyzPoints[0].x == Approx(1.0));
    REQUIRE(xyzPoints[0].y == Approx(2.0));
}

#ifdef LCAD_HAS_LASZIP

TEST_CASE("lazSupportAvailable reports true when built with LASzip", "[pointcloud][laz]") {
    REQUIRE(lcad::lazSupportAvailable());
}

TEST_CASE("readPointCloudLaz descales X/Y through the header's scale and offset", "[pointcloud][laz]") {
    TempLazPath temp;
    // Raw integers 100/200 with scale 0.01 and offset 5.0/-3.0 -> (6.0, -1.0).
    writeLazFile(temp.path, {{100, 200}, {300, -400}}, 0.01, 5.0, -3.0);

    const auto points = lcad::readPointCloudLaz(temp.path.string());
    REQUIRE(points.size() == 2);
    REQUIRE(points[0].x == Approx(6.0));
    REQUIRE(points[0].y == Approx(-1.0));
    REQUIRE(points[1].x == Approx(8.0));
    REQUIRE(points[1].y == Approx(-7.0));
}

TEST_CASE("readPointCloudLaz decimates evenly when over the cap", "[pointcloud][laz]") {
    TempLazPath temp;
    std::vector<std::pair<std::int32_t, std::int32_t>> raw;
    for (int i = 0; i < 1000; ++i) raw.emplace_back(i, 0);
    writeLazFile(temp.path, raw);

    const auto points = lcad::readPointCloudLaz(temp.path.string(), 100);
    REQUIRE(points.size() <= 101);
    REQUIRE(points.size() >= 90);
    REQUIRE(points.front().x == Approx(0.0));
    REQUIRE(points[1].x > points[0].x);
}

TEST_CASE("readPointCloudLaz returns empty for a missing or non-LAZ file", "[pointcloud][laz]") {
    REQUIRE(lcad::readPointCloudLaz("/no/such/file.laz").empty());

    TempLazPath temp;
    writeFile(temp.path, "not a laz file, way too short");
    REQUIRE(lcad::readPointCloudLaz(temp.path.string()).empty());
}

TEST_CASE("readPointCloudFile dispatches .laz to the LASzip reader", "[pointcloud][laz]") {
    TempLazPath temp;
    writeLazFile(temp.path, {{100, 200}}, 0.01, 0.0, 0.0);

    const auto points = lcad::readPointCloudFile(temp.path.string());
    REQUIRE(points.size() == 1);
    REQUIRE(points[0].x == Approx(1.0));
    REQUIRE(points[0].y == Approx(2.0));
}

#endif // LCAD_HAS_LASZIP
