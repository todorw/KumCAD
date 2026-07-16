#include "core/core3d/SheetMetal.h"

#include "core/geometry/Line.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Geom_Circle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>

namespace lcad {

namespace {

bool partIsWellFormed(const SheetMetalPart& part) {
    if (part.flatLengths.empty()) return false;
    if (part.flatLengths.size() != part.bendAngles.size() + 1) return false;
    if (part.width <= 1e-9 || part.thickness <= 1e-9) return false;
    for (double length : part.flatLengths) {
        if (length <= 1e-9) return false;
    }
    const double neutralRadius = part.bendRadius + part.kFactor * part.thickness;
    for (double angleDeg : part.bendAngles) {
        if (std::abs(angleDeg) < 1e-9 || std::abs(angleDeg) >= 180.0) return false;
        if (neutralRadius - part.thickness / 2.0 <= 1e-6) return false;
    }
    return true;
}

// The result of walking the strip's neutral axis: two full boundary edge
// chains (offset +/- thickness/2 from the neutral axis) plus the four
// corner points needed to cap the two open ends into one closed profile.
struct SpineWalk {
    std::vector<TopoDS_Edge> sideAEdges;
    std::vector<TopoDS_Edge> sideBEdges;
    gp_Pnt sideAStart, sideBStart, sideAEnd, sideBEnd;
    double neutralLength = 0.0;
};

// Walks flatLengths/bendAngles as a turtle-graphics path, emitting both
// offset boundaries. See SheetMetal.h for why a bend is a real arc (radius
// bendRadius + kFactor*thickness) rather than a sharp corner -- that's what
// makes the 3D solid's path length equal flatPatternLength()'s formula
// exactly rather than approximately.
SpineWalk walkSpine(const SheetMetalPart& part) {
    SpineWalk result;
    const double halfT = part.thickness / 2.0;
    double x = 0.0, y = 0.0, theta = 0.0;

    for (std::size_t i = 0; i < part.flatLengths.size(); ++i) {
        const double length = part.flatLengths[i];
        const double dirX = std::cos(theta), dirY = std::sin(theta);
        const double leftX = -dirY, leftY = dirX;
        const double startX = x, startY = y;
        const double endX = x + dirX * length, endY = y + dirY * length;

        const gp_Pnt sideAStart(startX + leftX * halfT, startY + leftY * halfT, 0.0);
        const gp_Pnt sideBStart(startX - leftX * halfT, startY - leftY * halfT, 0.0);
        const gp_Pnt sideAEnd(endX + leftX * halfT, endY + leftY * halfT, 0.0);
        const gp_Pnt sideBEnd(endX - leftX * halfT, endY - leftY * halfT, 0.0);

        result.sideAEdges.push_back(BRepBuilderAPI_MakeEdge(sideAStart, sideAEnd));
        result.sideBEdges.push_back(BRepBuilderAPI_MakeEdge(sideBStart, sideBEnd));
        result.neutralLength += length;

        if (i == 0) {
            result.sideAStart = sideAStart;
            result.sideBStart = sideBStart;
        }
        x = endX;
        y = endY;

        if (i + 1 < part.flatLengths.size()) {
            const double angleRad = part.bendAngles[i] * M_PI / 180.0;
            const double neutralRadius = part.bendRadius + part.kFactor * part.thickness;
            const double turnSign = angleRad > 0.0 ? 1.0 : -1.0;
            const double centerX = x + neutralRadius * turnSign * leftX;
            const double centerY = y + neutralRadius * turnSign * leftY;
            const double startAngle = std::atan2(y - centerY, x - centerX);
            const double endAngle = startAngle + angleRad;
            const double radialX = (x - centerX) / neutralRadius;
            const double radialY = (y - centerY) / neutralRadius;
            const double sign = leftX * radialX + leftY * radialY; // +1 or -1: is leftNormal outward or inward here

            const double sideARadius = neutralRadius + sign * halfT;
            const double sideBRadius = neutralRadius - sign * halfT;
            const double u1 = std::min(startAngle, endAngle);
            const double u2 = std::max(startAngle, endAngle);

            // XDirection is pinned to world +X so the circle's own
            // parameter is a plain polar angle, matching startAngle/
            // endAngle computed via atan2 -- gp_Ax2's 2-argument
            // constructor would pick an arbitrary XDirection instead,
            // silently breaking that correspondence.
            const gp_Ax2 axis(gp_Pnt(centerX, centerY, 0.0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
            Handle(Geom_Circle) circleA = new Geom_Circle(axis, sideARadius);
            Handle(Geom_Circle) circleB = new Geom_Circle(axis, sideBRadius);
            Handle(Geom_TrimmedCurve) trimmedA = new Geom_TrimmedCurve(circleA, u1, u2);
            Handle(Geom_TrimmedCurve) trimmedB = new Geom_TrimmedCurve(circleB, u1, u2);

            result.sideAEdges.push_back(BRepBuilderAPI_MakeEdge(trimmedA));
            result.sideBEdges.push_back(BRepBuilderAPI_MakeEdge(trimmedB));
            result.neutralLength += std::abs(angleRad) * neutralRadius;

            x = centerX + neutralRadius * std::cos(endAngle);
            y = centerY + neutralRadius * std::sin(endAngle);
            theta += angleRad;
        } else {
            result.sideAEnd = sideAEnd;
            result.sideBEnd = sideBEnd;
        }
    }
    return result;
}

} // namespace

TopoDS_Shape buildSheetMetalSolid(const SheetMetalPart& part) {
    if (!partIsWellFormed(part)) return TopoDS_Shape();

    const SpineWalk walk = walkSpine(part);

    BRepBuilderAPI_MakeWire wireBuilder;
    for (const TopoDS_Edge& edge : walk.sideAEdges) wireBuilder.Add(edge);
    wireBuilder.Add(BRepBuilderAPI_MakeEdge(walk.sideAEnd, walk.sideBEnd));
    for (auto it = walk.sideBEdges.rbegin(); it != walk.sideBEdges.rend(); ++it) wireBuilder.Add(*it);
    wireBuilder.Add(BRepBuilderAPI_MakeEdge(walk.sideBStart, walk.sideAStart));
    if (!wireBuilder.IsDone()) return TopoDS_Shape();

    BRepBuilderAPI_MakeFace faceBuilder(wireBuilder.Wire());
    if (!faceBuilder.IsDone()) return TopoDS_Shape();

    return BRepPrimAPI_MakePrism(faceBuilder.Face(), gp_Vec(0.0, 0.0, part.width)).Shape();
}

double flatPatternLength(const SheetMetalPart& part) {
    if (!partIsWellFormed(part)) return 0.0;
    return walkSpine(part).neutralLength;
}

void insertFlatPatternIntoDocument(Document& doc2d, const SheetMetalPart& part, double offsetX, double offsetY) {
    const double length = flatPatternLength(part);
    if (length <= 0.0) return;

    LayerId flatLayer = 0;
    bool found = false;
    for (const Layer& layer : doc2d.layers()) {
        if (layer.name == "FLATPATTERN") {
            flatLayer = layer.id;
            found = true;
            break;
        }
    }
    if (!found) flatLayer = doc2d.addLayer("FLATPATTERN", Color{255, 165, 0});

    auto addLine = [&](Point2D a, Point2D b, bool centerline) {
        const EntityId id = doc2d.reserveEntityId();
        auto line = std::make_unique<LineEntity>(id, flatLayer, Point2D(a.x + offsetX, a.y + offsetY),
                                                  Point2D(b.x + offsetX, b.y + offsetY));
        if (centerline) line->setLinetypeOverride(LineType::Center);
        doc2d.addEntity(std::move(line));
    };

    // The boundary rectangle.
    addLine(Point2D(0.0, 0.0), Point2D(length, 0.0), false);
    addLine(Point2D(length, 0.0), Point2D(length, part.width), false);
    addLine(Point2D(length, part.width), Point2D(0.0, part.width), false);
    addLine(Point2D(0.0, part.width), Point2D(0.0, 0.0), false);

    // One dashed bend line per bend, at its neutral-axis position along
    // the unfolded strip.
    double cumulative = part.flatLengths.empty() ? 0.0 : part.flatLengths[0];
    const double neutralRadius = part.bendRadius + part.kFactor * part.thickness;
    for (std::size_t i = 0; i < part.bendAngles.size(); ++i) {
        const double allowance = std::abs(part.bendAngles[i] * M_PI / 180.0) * neutralRadius;
        const double bendCenter = cumulative + allowance / 2.0;
        addLine(Point2D(bendCenter, 0.0), Point2D(bendCenter, part.width), true);
        cumulative += allowance;
        if (i + 1 < part.flatLengths.size()) cumulative += part.flatLengths[i + 1];
    }
}

} // namespace lcad
