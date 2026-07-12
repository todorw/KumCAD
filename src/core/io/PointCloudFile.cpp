#include "core/io/PointCloudFile.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace lcad {

namespace {

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

} // namespace lcad
