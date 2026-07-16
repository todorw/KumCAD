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

// Reads the ASPRS LAS binary format (versions 1.0-1.4; point data formats
// 0-10, since only the common 12-byte X/Y/Z prefix every format shares is
// read -- intensity/RGB/GPS-time/classification are skipped). X/Y are
// descaled per the header's scale+offset; Z is read into the record but
// dropped, same as readPointCloudXyz (KumCAD is a 2D system). Decimates
// the same way (every Nth point) when over maxPoints. Returns an empty
// vector if the file can't be opened or isn't a recognizable LAS file
// (bad "LASF" signature). LAZ (compressed LAS) is handled separately by
// readPointCloudLaz, since it needs a real LZ compression codec.
std::vector<Point2D> readPointCloudLas(const std::string& path, std::size_t maxPoints = 200000);

// True if this build was compiled with LASzip (LCAD_HAS_LASZIP), i.e.
// readPointCloudLaz can actually decompress ".laz" files rather than just
// returning empty.
bool lazSupportAvailable();

// Reads compressed LAS ("LAZ") point clouds via LASzip
// (https://laszip.org/, Apache-2.0), decoding the same X/Y/scale/offset
// fields as readPointCloudLas and decimating the same way. Returns an
// empty vector if the file can't be opened/isn't a LAZ file, or if this
// build has no LASzip support (see lazSupportAvailable()).
std::vector<Point2D> readPointCloudLaz(const std::string& path, std::size_t maxPoints = 200000);

// Dispatches to readPointCloudLas, readPointCloudLaz, or readPointCloudXyz
// by the path's file extension (case-insensitive ".las"/".laz", everything
// else treated as XYZ text).
std::vector<Point2D> readPointCloudFile(const std::string& path, std::size_t maxPoints = 200000);

} // namespace lcad
