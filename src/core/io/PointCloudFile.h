#pragma once

#include "core/geometry/Point2D.h"

#include <string>
#include <vector>

namespace lcad {

// Reads a plain-text XYZ point cloud (one point per line: "x y z" or
// "x,y,z", blank lines and lines starting with '#' ignored; Z is read but
// discarded -- KumCAD is a 2D system). This is the common lowest-common-
// -denominator export format point cloud tools agree on; the real binary
// formats (.rcs/.rcp/.las) would each need their own parser.
//
// Files are capped at maxPoints, decimating evenly (every Nth line) rather
// than truncating, so a huge scan still gives a representative preview
// instead of just its first corner. Returns an empty vector if the file
// can't be opened or has no readable points.
std::vector<Point2D> readPointCloudXyz(const std::string& path, std::size_t maxPoints = 200000);

} // namespace lcad
