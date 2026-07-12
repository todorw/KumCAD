#include "core/io/PointCloudFile.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>

namespace lcad {

namespace {

std::uint16_t getU16(const char* p) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(p[0])) |
           (static_cast<std::uint16_t>(static_cast<unsigned char>(p[1])) << 8);
}

std::uint32_t getU32(const char* p) {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(p[i]);
    return v;
}

std::uint64_t getU64(const char* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(p[i]);
    return v;
}

std::int32_t getI32(const char* p) { return static_cast<std::int32_t>(getU32(p)); }

double getF64(const char* p) {
    const std::uint64_t bits = getU64(p);
    double result = 0.0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

bool parseXyzLine(std::string line, Point2D& out) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == '#') return false;
    for (char& c : line) {
        if (c == ',') c = ' ';
    }
    std::istringstream iss(line);
    double x = 0.0, y = 0.0, z = 0.0;
    if (!(iss >> x >> y)) return false;
    iss >> z; // present in most XYZ dialects, but unused: KumCAD is 2D
    out = Point2D(x, y);
    return true;
}

} // namespace

std::vector<Point2D> readPointCloudXyz(const std::string& path, std::size_t maxPoints) {
    std::ifstream in(path);
    if (!in) return {};

    std::string line;
    Point2D tmp;
    std::size_t total = 0;
    while (std::getline(in, line)) {
        if (parseXyzLine(line, tmp)) ++total;
    }
    if (total == 0) return {};

    const std::size_t stride = std::max<std::size_t>(1, total / std::max<std::size_t>(1, maxPoints));

    in.clear();
    in.seekg(0);
    std::vector<Point2D> points;
    points.reserve(std::min(total, maxPoints) + 1);
    std::size_t index = 0;
    while (std::getline(in, line)) {
        if (!parseXyzLine(line, tmp)) continue;
        if (index % stride == 0) points.push_back(tmp);
        ++index;
    }
    return points;
}

std::vector<Point2D> readPointCloudLas(const std::string& path, std::size_t maxPoints) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    // Fields up to byte 227 have the same offsets across every LAS version
    // (1.3/1.4 only append fields after that), so a fixed-offset read of
    // this prefix works regardless of which version wrote the file.
    char header[227];
    in.read(header, sizeof(header));
    if (!in || std::memcmp(header, "LASF", 4) != 0) return {};

    const std::uint8_t versionMinor = static_cast<std::uint8_t>(header[25]);
    const std::uint32_t offsetToPointData = getU32(header + 96);
    const std::uint16_t recordLength = getU16(header + 105);
    std::uint64_t numPoints = getU32(header + 107);
    const double xScale = getF64(header + 131);
    const double yScale = getF64(header + 139);
    const double xOffset = getF64(header + 155);
    const double yOffset = getF64(header + 163);

    // LAS 1.4 files with more than ~4 billion points report 0 in the legacy
    // 32-bit count and carry the real count in an extended header field.
    if (numPoints == 0 && versionMinor >= 4) {
        char ext[8];
        in.seekg(247, std::ios::beg);
        if (in.read(ext, sizeof(ext))) numPoints = getU64(ext);
        in.clear();
    }

    if (numPoints == 0 || recordLength < 12) return {};

    in.seekg(offsetToPointData, std::ios::beg);
    if (!in) return {};

    const std::size_t stride =
        std::max<std::size_t>(1, static_cast<std::size_t>(numPoints) / std::max<std::size_t>(1, maxPoints));

    std::vector<Point2D> points;
    points.reserve(std::min<std::size_t>(static_cast<std::size_t>(numPoints), maxPoints) + 1);
    char xyz[12];
    for (std::uint64_t i = 0; i < numPoints && in; ++i) {
        in.read(xyz, sizeof(xyz));
        if (!in) break;
        if (i % stride == 0) {
            points.push_back(Point2D(getI32(xyz) * xScale + xOffset, getI32(xyz + 4) * yScale + yOffset));
        }
        if (recordLength > 12) in.ignore(recordLength - 12);
    }
    return points;
}

std::vector<Point2D> readPointCloudFile(const std::string& path, std::size_t maxPoints) {
    std::string ext;
    const auto dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot + 1);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == "las" ? readPointCloudLas(path, maxPoints) : readPointCloudXyz(path, maxPoints);
}

} // namespace lcad
