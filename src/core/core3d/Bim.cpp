#include "core/core3d/Bim.h"

#include "core/document/Document.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Table.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>

namespace lcad {

namespace {

// The transform that carries a horizontal member's own local frame (X
// along (x1,y1)-(x2,y2), Y across its width, Z up) into world space --
// shared by Wall and Beam (and openings, which use a Wall's own so
// offsetAlongWall/sillHeight line up with the wall they're cut into).
gp_Trsf segmentLocalToWorld(double x1, double y1, double x2, double y2) {
    const double angle = std::atan2(y2 - y1, x2 - x1);
    gp_Trsf rotate;
    rotate.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), angle);
    gp_Trsf translate;
    translate.SetTranslation(gp_Vec(x1, y1, 0.0));
    return translate.Multiplied(rotate);
}

gp_Trsf wallLocalToWorld(const Wall& wall) { return segmentLocalToWorld(wall.x1, wall.y1, wall.x2, wall.y2); }

// A point plus the centerline's own unit "along the wall" direction
// there -- what buildOpeningCutter needs to place a door/window frame at
// an arbitrary arc-length offset, whether the wall is the plain
// (x1,y1)-(x2,y2) case or a real multi-segment/curved path.
struct WallFrame {
    Point2D point;
    Point2D tangent;
};

gp_Trsf frameToWorld(const WallFrame& frame) {
    return segmentLocalToWorld(frame.point.x, frame.point.y, frame.point.x + frame.tangent.x,
                               frame.point.y + frame.tangent.y);
}

// Walks wall's own centerline by arc length (its path if set, else the
// plain (x1,y1)-(x2,y2) segment) and returns the point/tangent at
// distanceAlong, clamped to the centerline's own extent at either end --
// matching how a real BIM tool still places an opening past a wall's
// last vertex rather than rejecting it. nullopt only for a degenerate
// wall (zero-length fallback segment, or a path whose every segment is
// itself degenerate).
std::optional<WallFrame> wallFrameAtOffset(const Wall& wall, double distanceAlong) {
    if (wall.path.size() < 2) {
        const double dx = wall.x2 - wall.x1, dy = wall.y2 - wall.y1;
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1e-9) return std::nullopt;
        const Point2D tangent(dx / length, dy / length);
        const double t = std::clamp(distanceAlong, 0.0, length);
        return WallFrame{Point2D(wall.x1 + tangent.x * t, wall.y1 + tangent.y * t), tangent};
    }

    double remaining = std::max(0.0, distanceAlong);
    for (std::size_t i = 0; i + 1 < wall.path.size(); ++i) {
        const Point2D& a = wall.path[i];
        const Point2D& b = wall.path[i + 1];
        const bool lastSegment = i + 2 == wall.path.size();
        const double bulge = i < wall.bulges.size() ? wall.bulges[i] : 0.0;
        const auto arcOpt = bulgeToArc(a, b, bulge);
        if (!arcOpt) {
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double segLen = std::sqrt(dx * dx + dy * dy);
            if (segLen <= 1e-9) continue;
            if (remaining <= segLen || lastSegment) {
                const double t = std::clamp(remaining, 0.0, segLen);
                const Point2D tangent(dx / segLen, dy / segLen);
                return WallFrame{Point2D(a.x + tangent.x * t, a.y + tangent.y * t), tangent};
            }
            remaining -= segLen;
        } else {
            const BulgeArc& arc = *arcOpt;
            const double segLen = arc.radius * std::abs(arc.sweep);
            if (segLen <= 1e-9) continue;
            if (remaining <= segLen || lastSegment) {
                const double t = std::clamp(remaining, 0.0, segLen);
                const double sign = arc.sweep >= 0.0 ? 1.0 : -1.0;
                const double angle = arc.startAngle + sign * (t / arc.radius);
                const Point2D radial(std::cos(angle), std::sin(angle));
                const Point2D point(arc.center.x + arc.radius * radial.x, arc.center.y + arc.radius * radial.y);
                const Point2D tangent(-sign * radial.y, sign * radial.x); // radial rotated 90 deg, signed by sweep
                return WallFrame{point, tangent};
            }
            remaining -= segLen;
        }
    }
    return std::nullopt;
}

// Real OCCT edge for one bulged wall-path segment, at Z=0 -- same
// gp_Ax2-with-pinned-X-direction trick Document3D.cpp's own
// makeSweepPathArcEdge and SketchToFace.cpp's makeArcEdge use, so the
// trimmed circle's parametrization actually matches BulgeArc's own
// atan2-based startAngle/sweep rather than an implementation-defined X
// axis.
TopoDS_Edge makeWallBulgeArcEdge(const BulgeArc& arc) {
    const gp_Ax2 axis(gp_Pnt(arc.center.x, arc.center.y, 0.0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    Handle(Geom_Circle) circle = new Geom_Circle(axis, arc.radius);
    const double angle1 = arc.startAngle;
    const double angle2 = arc.startAngle + arc.sweep;
    Handle(Geom_TrimmedCurve) trimmed = new Geom_TrimmedCurve(circle, std::min(angle1, angle2), std::max(angle1, angle2));
    return BRepBuilderAPI_MakeEdge(trimmed).Edge();
}

// Sweeps a thickness x height rectangular profile along wall.path (a
// chain of straight/bulged-arc segments in the Z=0 plane, same
// convention as PolylineEntity) via MakePipeShell -- the same technique
// Document3D.cpp's own multi-segment/curved Sweep feature uses, RightCorner
// transition mode included so straight-run corners still miter cleanly.
// Returns a null shape if the path has fewer than 2 points or any
// segment is degenerate enough to fail wire-building.
TopoDS_Shape buildWallPathShape(const Wall& wall) {
    BRepBuilderAPI_MakeWire wireBuilder;
    for (std::size_t i = 0; i + 1 < wall.path.size(); ++i) {
        const Point2D& a = wall.path[i];
        const Point2D& b = wall.path[i + 1];
        const double bulge = i < wall.bulges.size() ? wall.bulges[i] : 0.0;
        const auto arcOpt = bulgeToArc(a, b, bulge);
        if (!arcOpt) {
            wireBuilder.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(a.x, a.y, 0.0), gp_Pnt(b.x, b.y, 0.0)));
        } else {
            wireBuilder.Add(makeWallBulgeArcEdge(*arcOpt));
        }
    }
    if (!wireBuilder.IsDone()) return TopoDS_Shape();

    const auto startFrame = wallFrameAtOffset(wall, 0.0);
    if (!startFrame) return TopoDS_Shape();
    const Point2D& p0 = startFrame->point;
    const Point2D n(-startFrame->tangent.y, startFrame->tangent.x); // horizontal, perpendicular to travel

    const gp_Pnt c1(p0.x - n.x * wall.thickness / 2.0, p0.y - n.y * wall.thickness / 2.0, 0.0);
    const gp_Pnt c2(p0.x + n.x * wall.thickness / 2.0, p0.y + n.y * wall.thickness / 2.0, 0.0);
    const gp_Pnt c3(c2.X(), c2.Y(), wall.height);
    const gp_Pnt c4(c1.X(), c1.Y(), wall.height);
    BRepBuilderAPI_MakeWire profileWireBuilder;
    profileWireBuilder.Add(BRepBuilderAPI_MakeEdge(c1, c2));
    profileWireBuilder.Add(BRepBuilderAPI_MakeEdge(c2, c3));
    profileWireBuilder.Add(BRepBuilderAPI_MakeEdge(c3, c4));
    profileWireBuilder.Add(BRepBuilderAPI_MakeEdge(c4, c1));
    if (!profileWireBuilder.IsDone()) return TopoDS_Shape();

    BRepOffsetAPI_MakePipeShell pipeBuilder(wireBuilder.Wire());
    pipeBuilder.SetTransitionMode(BRepBuilderAPI_RightCorner);
    pipeBuilder.Add(profileWireBuilder.Wire());
    pipeBuilder.Build();
    if (!pipeBuilder.IsDone()) return TopoDS_Shape();
    pipeBuilder.MakeSolid();
    return pipeBuilder.Shape();
}

TopoDS_Shape buildWallShape(const Wall& wall) {
    if (wall.thickness <= 1e-9 || wall.height <= 1e-9) return TopoDS_Shape();
    if (wall.path.size() >= 2) return buildWallPathShape(wall);

    const double dx = wall.x2 - wall.x1, dy = wall.y2 - wall.y1;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1e-9) return TopoDS_Shape();

    const TopoDS_Shape box =
        BRepPrimAPI_MakeBox(gp_Pnt(0.0, -wall.thickness / 2.0, 0.0), length, wall.thickness, wall.height).Shape();
    return BRepBuilderAPI_Transform(box, wallLocalToWorld(wall), true).Shape();
}

TopoDS_Shape buildColumnShape(const Column& column) {
    if (column.height <= 1e-9) return TopoDS_Shape();
    if (column.round) {
        const double radius = column.width / 2.0;
        if (radius <= 1e-9) return TopoDS_Shape();
        const gp_Ax2 axis(gp_Pnt(column.x, column.y, column.baseElevation), gp_Dir(0, 0, 1));
        return BRepPrimAPI_MakeCylinder(axis, radius, column.height).Shape();
    }
    if (column.width <= 1e-9 || column.depth <= 1e-9) return TopoDS_Shape();
    return BRepPrimAPI_MakeBox(gp_Pnt(column.x - column.width / 2.0, column.y - column.depth / 2.0, column.baseElevation),
                              column.width, column.depth, column.height)
        .Shape();
}

TopoDS_Shape buildBeamShape(const Beam& beam) {
    const double dx = beam.x2 - beam.x1, dy = beam.y2 - beam.y1;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1e-9 || beam.width <= 1e-9 || beam.depth <= 1e-9) return TopoDS_Shape();

    const TopoDS_Shape box =
        BRepPrimAPI_MakeBox(gp_Pnt(0.0, -beam.width / 2.0, beam.elevation), length, beam.width, beam.depth).Shape();
    return BRepBuilderAPI_Transform(box, segmentLocalToWorld(beam.x1, beam.y1, beam.x2, beam.y2), true).Shape();
}

TopoDS_Shape buildOpeningCutter(const Wall& wall, const Opening& opening) {
    if (opening.width <= 1e-9 || opening.height <= 1e-9) return TopoDS_Shape();
    const auto frame = wallFrameAtOffset(wall, opening.offsetAlongWall);
    if (!frame) return TopoDS_Shape();

    // Deliberately taller across the wall's thickness than the wall itself
    // (2x), so the cut always fully punches through regardless of floating
    // point tolerance at the wall's own faces. Built at local X=0 (the
    // opening's own arc-length offset is already baked into frame's own
    // point) rather than at X=offsetAlongWall the way a single straight
    // segment's own local frame would need -- frameToWorld's translation
    // already IS that offset, for either a straight or curved wall.
    const double cutDepth = wall.thickness * 2.0;
    const TopoDS_Shape box =
        BRepPrimAPI_MakeBox(gp_Pnt(0.0, -cutDepth / 2.0, opening.sillHeight), opening.width, cutDepth, opening.height)
            .Shape();
    return BRepBuilderAPI_Transform(box, frameToWorld(*frame), true).Shape();
}

TopoDS_Shape buildSlabShape(const Slab& slab) {
    if (slab.boundary.size() < 3 || slab.thickness <= 1e-9) return TopoDS_Shape();

    BRepBuilderAPI_MakePolygon polygon;
    for (const auto& [x, y] : slab.boundary) polygon.Add(gp_Pnt(x, y, 0.0));
    polygon.Close();
    if (!polygon.IsDone()) return TopoDS_Shape();

    BRepBuilderAPI_MakeFace faceBuilder(polygon.Wire());
    if (!faceBuilder.IsDone()) return TopoDS_Shape();

    TopoDS_Shape prism = BRepPrimAPI_MakePrism(faceBuilder.Face(), gp_Vec(0.0, 0.0, slab.thickness)).Shape();
    if (std::abs(slab.elevation) > 1e-12) {
        gp_Trsf move;
        move.SetTranslation(gp_Vec(0.0, 0.0, slab.elevation));
        prism = BRepBuilderAPI_Transform(prism, move, true).Shape();
    }
    return prism;
}

std::vector<double> splitArgs(const std::string& args) {
    std::vector<double> values;
    std::stringstream ss(args);
    std::string token;
    while (std::getline(ss, token, ',')) {
        try {
            values.push_back(std::stod(token));
        } catch (const std::exception&) {
            // A malformed/non-numeric argument -- skip it rather than
            // aborting the whole read; the caller's own size check on
            // values.size() will reject the entity if this matters.
        }
    }
    return values;
}

// Real STEP/IFC string-literal escaping (ISO 10303-21's own convention,
// the same format this file's IFCSPACE line already claims to be a
// "real subset" of): a literal apostrophe inside a quoted string is
// written as two apostrophes in a row, not backslash-escaped the way
// C-family languages do it.
std::string escapeStepString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out += c;
        if (c == '\'') out += c;
    }
    return out;
}

std::string unescapeStepString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\'' && i + 1 < s.size() && s[i + 1] == '\'') {
            out += '\'';
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Finds the real closing quote starting the search at start: a lone
// apostrophe, skipping over any escaped '' pair along the way. Returns
// std::string::npos if the string never closes.
std::size_t findClosingQuote(const std::string& s, std::size_t start) {
    for (std::size_t i = start; i < s.size(); ++i) {
        if (s[i] != '\'') continue;
        if (i + 1 < s.size() && s[i + 1] == '\'') {
            ++i;
            continue;
        }
        return i;
    }
    return std::string::npos;
}

} // namespace

BimShapes buildBimShapes(const BimModel& model) {
    BimShapes result;
    result.wallShapes.resize(model.walls.size());
    for (std::size_t i = 0; i < model.walls.size(); ++i) result.wallShapes[i] = buildWallShape(model.walls[i]);

    for (const Opening& opening : model.openings) {
        if (opening.wallIndex < 0 || opening.wallIndex >= static_cast<int>(result.wallShapes.size())) continue;
        TopoDS_Shape& wallShape = result.wallShapes[static_cast<std::size_t>(opening.wallIndex)];
        if (wallShape.IsNull()) continue;
        const TopoDS_Shape cutter = buildOpeningCutter(model.walls[static_cast<std::size_t>(opening.wallIndex)], opening);
        if (cutter.IsNull()) continue;
        BRepAlgoAPI_Cut cut(wallShape, cutter);
        if (cut.IsDone()) wallShape = cut.Shape();
    }

    result.slabShapes.resize(model.slabs.size());
    for (std::size_t i = 0; i < model.slabs.size(); ++i) result.slabShapes[i] = buildSlabShape(model.slabs[i]);

    result.columnShapes.resize(model.columns.size());
    for (std::size_t i = 0; i < model.columns.size(); ++i) result.columnShapes[i] = buildColumnShape(model.columns[i]);

    result.beamShapes.resize(model.beams.size());
    for (std::size_t i = 0; i < model.beams.size(); ++i) result.beamShapes[i] = buildBeamShape(model.beams[i]);

    return result;
}

TopoDS_Shape combinedBimShape(const BimShapes& shapes) {
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    bool any = false;
    for (const auto& shape : shapes.wallShapes) {
        if (!shape.IsNull()) {
            builder.Add(compound, shape);
            any = true;
        }
    }
    for (const auto& shape : shapes.slabShapes) {
        if (!shape.IsNull()) {
            builder.Add(compound, shape);
            any = true;
        }
    }
    for (const auto& shape : shapes.columnShapes) {
        if (!shape.IsNull()) {
            builder.Add(compound, shape);
            any = true;
        }
    }
    for (const auto& shape : shapes.beamShapes) {
        if (!shape.IsNull()) {
            builder.Add(compound, shape);
            any = true;
        }
    }
    if (!any) return TopoDS_Shape();
    return compound;
}

bool writeIfcLite(const BimModel& model, const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;

    out << "ISO-10303-21;\n";
    out << "HEADER;\n";
    out << "FILE_DESCRIPTION(('KumCAD IFC-lite export -- not standard IFC, see Bim.h'),'2;1');\n";
    out << "FILE_NAME('','',(''),(''),'KumCAD','KumCAD','');\n";
    out << "FILE_SCHEMA(('KUMCAD_IFC_LITE'));\n";
    out << "ENDSEC;\n";
    out << "DATA;\n";

    int id = 1;
    for (const Wall& wall : model.walls) {
        if (wall.path.size() >= 2) {
            // x1/y1/x2/y2 kept as path.front()/path.back() so an older
            // reader (or anything only reading the first 6 args) still
            // gets a sane straight-line stand-in for this wall's overall
            // extent rather than nothing at all.
            out << '#' << id++ << "=IFCWALL(" << wall.path.front().x << ',' << wall.path.front().y << ','
                << wall.path.back().x << ',' << wall.path.back().y << ',' << wall.height << ',' << wall.thickness
                << ',' << wall.path.size();
            for (std::size_t i = 0; i < wall.path.size(); ++i) {
                const double bulge = i < wall.bulges.size() ? wall.bulges[i] : 0.0;
                out << ',' << wall.path[i].x << ',' << wall.path[i].y << ',' << bulge;
            }
            out << ");\n";
        } else {
            out << '#' << id++ << "=IFCWALL(" << wall.x1 << ',' << wall.y1 << ',' << wall.x2 << ',' << wall.y2 << ','
                << wall.height << ',' << wall.thickness << ");\n";
        }
    }
    for (const Opening& opening : model.openings) {
        out << '#' << id++ << "=IFCOPENING(" << opening.wallIndex << ',' << opening.offsetAlongWall << ','
            << opening.width << ',' << opening.height << ',' << opening.sillHeight << ',' << (opening.isWindow ? 1 : 0)
            << ");\n";
    }
    for (const Slab& slab : model.slabs) {
        out << '#' << id++ << "=IFCSLAB(" << slab.thickness << ',' << slab.elevation << ',' << slab.boundary.size();
        for (const auto& [x, y] : slab.boundary) out << ',' << x << ',' << y;
        out << ");\n";
    }
    for (const Column& column : model.columns) {
        out << '#' << id++ << "=IFCCOLUMN(" << column.x << ',' << column.y << ',' << column.baseElevation << ','
            << column.height << ',' << column.width << ',' << column.depth << ',' << (column.round ? 1 : 0) << ");\n";
    }
    for (const Beam& beam : model.beams) {
        out << '#' << id++ << "=IFCBEAM(" << beam.x1 << ',' << beam.y1 << ',' << beam.x2 << ',' << beam.y2 << ','
            << beam.elevation << ',' << beam.width << ',' << beam.depth << ");\n";
    }
    for (const Space& space : model.spaces) {
        out << '#' << id++ << "=IFCSPACE('" << escapeStepString(space.name) << "'," << space.boundary.size();
        for (const auto& [x, y] : space.boundary) out << ',' << x << ',' << y;
        out << ");\n";
    }

    out << "ENDSEC;\n";
    out << "END-ISO-10303-21;\n";
    return static_cast<bool>(out);
}

bool readIfcLite(BimModel& model, const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    bool inData = false;
    while (std::getline(in, line)) {
        if (line.find("DATA;") != std::string::npos) {
            inData = true;
            continue;
        }
        if (line.find("ENDSEC;") != std::string::npos) {
            inData = false;
            continue;
        }
        if (!inData) continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const auto open = line.find('(', eq);
        const auto close = line.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close < open) continue;

        const std::string name = line.substr(eq + 1, open - (eq + 1));
        const std::vector<double> values = splitArgs(line.substr(open + 1, close - open - 1));

        if (name == "IFCWALL" && values.size() >= 6) {
            Wall wall;
            wall.x1 = values[0];
            wall.y1 = values[1];
            wall.x2 = values[2];
            wall.y2 = values[3];
            wall.height = values[4];
            wall.thickness = values[5];
            if (values.size() > 6) {
                const auto n = static_cast<std::size_t>(values[6]);
                if (values.size() == 7 + 3 * n) {
                    for (std::size_t i = 0; i < n; ++i) {
                        wall.path.emplace_back(values[7 + 3 * i], values[7 + 3 * i + 1]);
                        wall.bulges.push_back(values[7 + 3 * i + 2]);
                    }
                }
                // Malformed path extension: keep the straight x1..y2
                // fallback already set above rather than dropping the
                // whole wall, the same spirit as this file's other
                // malformed-entity handling (e.g. IFCSLAB's point count).
            }
            model.walls.push_back(wall);
        } else if (name == "IFCOPENING" && values.size() == 6) {
            Opening opening;
            opening.wallIndex = static_cast<int>(values[0]);
            opening.offsetAlongWall = values[1];
            opening.width = values[2];
            opening.height = values[3];
            opening.sillHeight = values[4];
            opening.isWindow = values[5] != 0.0;
            model.openings.push_back(opening);
        } else if (name == "IFCSLAB" && values.size() >= 3) {
            Slab slab;
            slab.thickness = values[0];
            slab.elevation = values[1];
            const auto n = static_cast<std::size_t>(values[2]);
            if (values.size() != 3 + 2 * n) continue; // malformed point count -- skip this entity
            for (std::size_t i = 0; i < n; ++i) slab.boundary.emplace_back(values[3 + 2 * i], values[3 + 2 * i + 1]);
            model.slabs.push_back(slab);
        } else if (name == "IFCCOLUMN" && values.size() == 7) {
            Column column;
            column.x = values[0];
            column.y = values[1];
            column.baseElevation = values[2];
            column.height = values[3];
            column.width = values[4];
            column.depth = values[5];
            column.round = values[6] != 0.0;
            model.columns.push_back(column);
        } else if (name == "IFCBEAM" && values.size() == 7) {
            Beam beam;
            beam.x1 = values[0];
            beam.y1 = values[1];
            beam.x2 = values[2];
            beam.y2 = values[3];
            beam.elevation = values[4];
            beam.width = values[5];
            beam.depth = values[6];
            model.beams.push_back(beam);
        } else if (name == "IFCSPACE") {
            const std::string args = line.substr(open + 1, close - open - 1);
            const auto q1 = args.find('\'');
            const auto q2 = q1 == std::string::npos ? std::string::npos : findClosingQuote(args, q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) continue;
            const auto afterQuote = args.find(',', q2);
            if (afterQuote == std::string::npos) continue;
            const std::vector<double> spaceValues = splitArgs(args.substr(afterQuote + 1));
            if (spaceValues.empty()) continue;
            const auto n = static_cast<std::size_t>(spaceValues[0]);
            if (spaceValues.size() != 1 + 2 * n) continue;
            Space space;
            space.name = unescapeStepString(args.substr(q1 + 1, q2 - q1 - 1));
            for (std::size_t i = 0; i < n; ++i) space.boundary.emplace_back(spaceValues[1 + 2 * i], spaceValues[1 + 2 * i + 1]);
            model.spaces.push_back(space);
        }
    }
    return true;
}

TableEntity* buildOpeningScheduleTable(Document& doc2d, const BimModel& model, Point2D position) {
    const int rowCount = static_cast<int>(model.openings.size()) + 1;
    std::vector<double> rowHeights(static_cast<std::size_t>(rowCount), 4.0);
    std::vector<double> colWidths = {15.0, 12.0, 15.0, 15.0, 15.0};
    std::vector<std::string> cells(static_cast<std::size_t>(rowCount) * 5);

    cells[0] = "Type";
    cells[1] = "Wall #";
    cells[2] = "Width";
    cells[3] = "Height";
    cells[4] = "Sill";

    for (std::size_t i = 0; i < model.openings.size(); ++i) {
        const Opening& opening = model.openings[i];
        const std::size_t row = i + 1;
        cells[row * 5 + 0] = opening.isWindow ? "Window" : "Door";
        cells[row * 5 + 1] = std::to_string(opening.wallIndex);
        cells[row * 5 + 2] = std::to_string(opening.width);
        cells[row * 5 + 3] = std::to_string(opening.height);
        cells[row * 5 + 4] = std::to_string(opening.sillHeight);
    }

    auto table = std::make_unique<TableEntity>(doc2d.reserveEntityId(), doc2d.currentLayer(), position, rowHeights,
                                                colWidths, cells);
    TableEntity* raw = table.get();
    doc2d.addEntity(std::move(table));
    return raw;
}

namespace {
// Shoelace formula, assuming a simple (non-self-intersecting) polygon --
// the same assumption Slab's own boundary already makes.
double polygonArea(const std::vector<std::pair<double, double>>& boundary) {
    double sum = 0.0;
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        const auto& [x0, y0] = boundary[i];
        const auto& [x1, y1] = boundary[(i + 1) % boundary.size()];
        sum += x0 * y1 - x1 * y0;
    }
    return std::abs(sum) / 2.0;
}

double polygonPerimeter(const std::vector<std::pair<double, double>>& boundary) {
    double sum = 0.0;
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        const auto& [x0, y0] = boundary[i];
        const auto& [x1, y1] = boundary[(i + 1) % boundary.size()];
        sum += std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    }
    return sum;
}
} // namespace

TableEntity* buildRoomScheduleTable(Document& doc2d, const BimModel& model, Point2D position) {
    const int rowCount = static_cast<int>(model.spaces.size()) + 1;
    std::vector<double> rowHeights(static_cast<std::size_t>(rowCount), 4.0);
    std::vector<double> colWidths = {20.0, 15.0, 15.0};
    std::vector<std::string> cells(static_cast<std::size_t>(rowCount) * 3);

    cells[0] = "Name";
    cells[1] = "Area";
    cells[2] = "Perimeter";

    for (std::size_t i = 0; i < model.spaces.size(); ++i) {
        const Space& space = model.spaces[i];
        const std::size_t row = i + 1;
        cells[row * 3 + 0] = space.name;
        cells[row * 3 + 1] = space.boundary.size() >= 3 ? std::to_string(polygonArea(space.boundary)) : "0";
        cells[row * 3 + 2] = space.boundary.size() >= 3 ? std::to_string(polygonPerimeter(space.boundary)) : "0";
    }

    auto table = std::make_unique<TableEntity>(doc2d.reserveEntityId(), doc2d.currentLayer(), position, rowHeights,
                                                colWidths, cells);
    TableEntity* raw = table.get();
    doc2d.addEntity(std::move(table));
    return raw;
}

} // namespace lcad
