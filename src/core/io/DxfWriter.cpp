#include "core/io/DxfWriter.h"

#include "core/geometry/Arc.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/Circle.h"
#include "core/geometry/ConstructionLine.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Image.h"
#include "core/geometry/Insert.h"
#include "core/geometry/PointCloud.h"
#include "core/geometry/Leader.h"
#include "core/geometry/Line.h"
#include "core/geometry/MLeader.h"
#include "core/geometry/MText.h"
#include "core/geometry/PointEnt.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Table.h"
#include "core/geometry/Text.h"
#include "core/io/DxfColors.h"

#include <cmath>
#include <fstream>

namespace lcad {

namespace {

void writeGroup(std::ofstream& out, int code, const std::string& value) {
    out << code << "\n" << value << "\n";
}

void writeGroup(std::ofstream& out, int code, double value) {
    out << code << "\n" << value << "\n";
}

void writeGroup(std::ofstream& out, int code, int value) {
    out << code << "\n" << value << "\n";
}

int trueColor(const Color& c) {
    return (static_cast<int>(c.r) << 16) | (static_cast<int>(c.g) << 8) | static_cast<int>(c.b);
}

std::string layerName(const Document& doc, LayerId id) {
    if (const Layer* layer = doc.findLayer(id)) return layer->name;
    return "0";
}

// Layer reference plus the optional per-entity color/linetype overrides --
// the groups every entity type shares right after its "0 <TYPE>" record.
void writeCommon(std::ofstream& out, const Document& doc, const Entity& e) {
    writeGroup(out, 8, layerName(doc, e.layer()));
    if (const auto& linetype = e.linetypeOverride()) {
        writeGroup(out, 6, lineTypeName(*linetype));
    }
    if (const auto& color = e.colorOverride()) {
        writeGroup(out, 62, colorToAci(*color));
        writeGroup(out, 420, trueColor(*color));
    }
    if (const auto& weight = e.lineweightOverride()) {
        writeGroup(out, 370, static_cast<int>(std::lround(*weight * 100.0)));
    }
}

void writeEntity(std::ofstream& out, const Document& document, const Entity& e) {
    switch (e.type()) {
    case EntityType::Line: {
        const auto& line = static_cast<const LineEntity&>(e);
        writeGroup(out, 0, "LINE");
        writeCommon(out, document, e);
        writeGroup(out, 10, line.start().x);
        writeGroup(out, 20, line.start().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 11, line.end().x);
        writeGroup(out, 21, line.end().y);
        writeGroup(out, 31, 0.0);
        break;
    }
    case EntityType::Circle: {
        const auto& circle = static_cast<const CircleEntity&>(e);
        writeGroup(out, 0, "CIRCLE");
        writeCommon(out, document, e);
        writeGroup(out, 10, circle.center().x);
        writeGroup(out, 20, circle.center().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 40, circle.radius());
        break;
    }
    case EntityType::Arc: {
        const auto& arc = static_cast<const ArcEntity&>(e);
        writeGroup(out, 0, "ARC");
        writeCommon(out, document, e);
        writeGroup(out, 10, arc.center().x);
        writeGroup(out, 20, arc.center().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 40, arc.radius());
        writeGroup(out, 50, arc.startAngle() * 180.0 / M_PI);
        writeGroup(out, 51, arc.endAngle() * 180.0 / M_PI);
        break;
    }
    case EntityType::Polyline: {
        const auto& pl = static_cast<const PolylineEntity&>(e);
        writeGroup(out, 0, "LWPOLYLINE");
        writeCommon(out, document, e);
        writeGroup(out, 90, static_cast<int>(pl.vertices().size()));
        writeGroup(out, 70, pl.closed() ? 1 : 0);
        for (std::size_t i = 0; i < pl.vertices().size(); ++i) {
            writeGroup(out, 10, pl.vertices()[i].x);
            writeGroup(out, 20, pl.vertices()[i].y);
            if (std::abs(pl.bulgeAt(i)) > 1e-9) writeGroup(out, 42, pl.bulgeAt(i));
        }
        break;
    }
    case EntityType::Ellipse: {
        const auto& ellipse = static_cast<const EllipseEntity&>(e);
        const bool xIsMajor = ellipse.radiusX() >= ellipse.radiusY();
        const double majorRadius = xIsMajor ? ellipse.radiusX() : ellipse.radiusY();
        const double minorRadius = xIsMajor ? ellipse.radiusY() : ellipse.radiusX();
        const double ratio = majorRadius > 1e-12 ? minorRadius / majorRadius : 1.0;
        // 11/21 is the major axis endpoint relative to the center; its
        // direction encodes the ellipse's rotation.
        const double majorDir = xIsMajor ? ellipse.rotation() : ellipse.rotation() + M_PI / 2;
        writeGroup(out, 0, "ELLIPSE");
        writeCommon(out, document, e);
        writeGroup(out, 10, ellipse.center().x);
        writeGroup(out, 20, ellipse.center().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 11, majorRadius * std::cos(majorDir));
        writeGroup(out, 21, majorRadius * std::sin(majorDir));
        writeGroup(out, 31, 0.0);
        writeGroup(out, 40, ratio);
        writeGroup(out, 41, 0.0);        // start parameter: full ellipse
        writeGroup(out, 42, 2.0 * M_PI); // end parameter
        break;
    }
    case EntityType::Spline: {
        const auto& spline = static_cast<const SplineEntity&>(e);
        writeGroup(out, 0, "SPLINE");
        writeCommon(out, document, e);
        writeGroup(out, 70, 8); // planar
        writeGroup(out, 71, spline.degree());
        writeGroup(out, 72, static_cast<int>(spline.knots().size()));
        writeGroup(out, 73, static_cast<int>(spline.controlPoints().size()));
        writeGroup(out, 74, static_cast<int>(spline.fitPoints().size()));
        for (double knot : spline.knots()) writeGroup(out, 40, knot);
        for (const Point2D& p : spline.controlPoints()) {
            writeGroup(out, 10, p.x);
            writeGroup(out, 20, p.y);
            writeGroup(out, 30, 0.0);
        }
        for (const Point2D& p : spline.fitPoints()) {
            writeGroup(out, 11, p.x);
            writeGroup(out, 21, p.y);
            writeGroup(out, 31, 0.0);
        }
        break;
    }
    case EntityType::Dimension: {
        const auto& dim = static_cast<const DimensionEntity&>(e);
        writeGroup(out, 0, "DIMENSION");
        writeCommon(out, document, e);
        // Bit 32 = block-reference flag that AutoCAD always sets.
        switch (dim.kind()) {
        case DimensionKind::Linear:
        case DimensionKind::Aligned:
            writeGroup(out, 10, dim.linePoint().x); // definition point on the dimension line
            writeGroup(out, 20, dim.linePoint().y);
            writeGroup(out, 30, 0.0);
            writeGroup(out, 13, dim.point1().x); // first extension line origin
            writeGroup(out, 23, dim.point1().y);
            writeGroup(out, 33, 0.0);
            writeGroup(out, 14, dim.point2().x); // second extension line origin
            writeGroup(out, 24, dim.point2().y);
            writeGroup(out, 34, 0.0);
            writeGroup(out, 70, dim.aligned() ? 33 : 32);
            break;
        case DimensionKind::Radius:
        case DimensionKind::Diameter: {
            // Radial: 10 = center, 15 = point on the curve. Diameter: 10 and
            // 15 are opposite chord points.
            const bool diameter = dim.kind() == DimensionKind::Diameter;
            const Point2D first =
                diameter ? dim.point1() * 2.0 - dim.point2() : dim.point1();
            writeGroup(out, 10, first.x);
            writeGroup(out, 20, first.y);
            writeGroup(out, 30, 0.0);
            writeGroup(out, 15, dim.point2().x);
            writeGroup(out, 25, dim.point2().y);
            writeGroup(out, 35, 0.0);
            writeGroup(out, 11, dim.linePoint().x); // text midpoint
            writeGroup(out, 21, dim.linePoint().y);
            writeGroup(out, 31, 0.0);
            writeGroup(out, 40, 0.0); // leader length
            writeGroup(out, 70, (diameter ? 3 : 4) | 32);
            break;
        }
        case DimensionKind::Angular:
            // 3-point angular: 13/14 = ray points, 15 = vertex, 10 = point
            // fixing the arc's position.
            writeGroup(out, 10, dim.linePoint().x);
            writeGroup(out, 20, dim.linePoint().y);
            writeGroup(out, 30, 0.0);
            writeGroup(out, 13, dim.point1().x);
            writeGroup(out, 23, dim.point1().y);
            writeGroup(out, 33, 0.0);
            writeGroup(out, 14, dim.point2().x);
            writeGroup(out, 24, dim.point2().y);
            writeGroup(out, 34, 0.0);
            writeGroup(out, 15, dim.vertex().x);
            writeGroup(out, 25, dim.vertex().y);
            writeGroup(out, 35, 0.0);
            writeGroup(out, 70, 5 | 32);
            break;
        }
        writeGroup(out, 140, dim.textHeight()); // dim style text-height override
        break;
    }
    case EntityType::Text: {
        const auto& text = static_cast<const TextEntity&>(e);
        writeGroup(out, 0, "TEXT");
        writeCommon(out, document, e);
        writeGroup(out, 10, text.position().x);
        writeGroup(out, 20, text.position().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 40, text.height());
        writeGroup(out, 1, text.text());
        writeGroup(out, 50, text.rotation() * 180.0 / M_PI);
        writeGroup(out, 7, text.styleName());
        break;
    }
    case EntityType::Leader: {
        const auto& leader = static_cast<const LeaderEntity&>(e);
        writeGroup(out, 0, "LEADER");
        writeCommon(out, document, e);
        writeGroup(out, 71, 1); // arrowhead on
        writeGroup(out, 72, 0); // straight segments
        writeGroup(out, 73, 3); // no annotation
        writeGroup(out, 76, static_cast<int>(leader.points().size()));
        for (const Point2D& p : leader.points()) {
            writeGroup(out, 10, p.x);
            writeGroup(out, 20, p.y);
            writeGroup(out, 30, 0.0);
        }
        break;
    }
    case EntityType::Table: {
        // Simplified ACAD_TABLE: real AutoCAD tables carry a TABLESTYLE
        // object plus per-cell formatting/fields/merges, which is a much
        // bigger project. This round-trips cleanly within KumCAD (readDxf
        // below); other DXF readers will skip the unrecognized entity type
        // rather than error, per the DXF spec's forward-compatibility rule.
        const auto& table = static_cast<const TableEntity&>(e);
        writeGroup(out, 0, "ACAD_TABLE");
        writeCommon(out, document, e);
        writeGroup(out, 10, table.position().x);
        writeGroup(out, 20, table.position().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 40, table.textHeight());
        writeGroup(out, 90, table.rows());
        writeGroup(out, 91, table.cols());
        for (double h : table.rowHeights()) writeGroup(out, 141, h);
        for (double w : table.colWidths()) writeGroup(out, 142, w);
        for (int r = 0; r < table.rows(); ++r) {
            for (int c = 0; c < table.cols(); ++c) writeGroup(out, 1, table.cellText(r, c));
        }
        break;
    }
    case EntityType::MLeader: {
        // Simplified MULTILEADER: real DXF MULTILEADER carries its geometry
        // inside a large "context data" block plus a MLEADERSTYLE object
        // reference, which is a much bigger project. This round-trips
        // cleanly within KumCAD; other readers skip the unrecognized layout
        // rather than error, same tradeoff as ACAD_TABLE above.
        const auto& mleader = static_cast<const MLeaderEntity&>(e);
        writeGroup(out, 0, "MULTILEADER");
        writeCommon(out, document, e);
        writeGroup(out, 10, mleader.landing().x);
        writeGroup(out, 20, mleader.landing().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 40, mleader.arrowSize());
        for (const auto& leg : mleader.legs()) {
            writeGroup(out, 70, static_cast<int>(leg.size()));
            for (const Point2D& p : leg) {
                writeGroup(out, 11, p.x);
                writeGroup(out, 21, p.y);
                writeGroup(out, 31, 0.0);
            }
        }
        break;
    }
    case EntityType::Image: {
        // Simplified IMAGE: real DXF IMAGE references a separate IMAGEDEF
        // object (in the OBJECTS section) by handle for the file path and
        // pixel size; this carries the path directly on the entity, which
        // round-trips within KumCAD and is safely skipped by other readers.
        const auto& image = static_cast<const ImageEntity&>(e);
        writeGroup(out, 0, "IMAGE");
        writeCommon(out, document, e);
        writeGroup(out, 1, image.path());
        writeGroup(out, 10, image.position().x);
        writeGroup(out, 20, image.position().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 40, image.width());
        writeGroup(out, 41, image.height());
        writeGroup(out, 50, image.rotation() * 180.0 / M_PI);
        break;
    }
    case EntityType::PointCloud: {
        // Path only, like a real POINTCLOUD's reference to an external
        // .rcs/.rcp file: with realistic cloud sizes, embedding every point
        // as DXF group codes (the way LWPOLYLINE embeds its vertices) would
        // make the file enormous. readDxf re-reads the source XYZ file when
        // it's reachable; if not, the cloud round-trips with no points
        // rather than XREF's cached-snapshot fallback.
        const auto& cloud = static_cast<const PointCloudEntity&>(e);
        writeGroup(out, 0, "POINTCLOUD");
        writeCommon(out, document, e);
        writeGroup(out, 1, cloud.path());
        break;
    }
    case EntityType::MText: {
        const auto& mtext = static_cast<const MTextEntity&>(e);
        writeGroup(out, 0, "MTEXT");
        writeCommon(out, document, e);
        writeGroup(out, 10, mtext.position().x);
        writeGroup(out, 20, mtext.position().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 40, mtext.height());
        writeGroup(out, 41, mtext.width());
        writeGroup(out, 71, 1); // attachment: top left
        writeGroup(out, 72, 1); // drawing direction: left to right
        // Long content goes out as 250-char chunks in group 3 with the tail
        // in group 1, per the DXF spec.
        const std::string encoded = encodeMTextContent(mtext.text());
        std::size_t pos = 0;
        while (encoded.size() - pos > 250) {
            writeGroup(out, 3, encoded.substr(pos, 250));
            pos += 250;
        }
        writeGroup(out, 1, encoded.substr(pos));
        writeGroup(out, 50, mtext.rotation() * 180.0 / M_PI);
        writeGroup(out, 7, mtext.styleName());
        break;
    }
    case EntityType::Hatch: {
        const auto& hatch = static_cast<const HatchEntity&>(e);
        const bool solid = hatch.pattern() == HatchPattern::Solid;
        writeGroup(out, 0, "HATCH");
        writeCommon(out, document, e);
        writeGroup(out, 2, hatchPatternName(hatch.pattern()));
        writeGroup(out, 70, solid ? 1 : 0); // solid vs pattern fill
        writeGroup(out, 71, 0);             // non-associative
        writeGroup(out, 91, 1);             // one boundary path
        writeGroup(out, 92, 2);             // path type: polyline
        writeGroup(out, 72, 0);             // no bulges
        writeGroup(out, 73, 1);             // closed
        writeGroup(out, 93, static_cast<int>(hatch.vertices().size()));
        for (const Point2D& v : hatch.vertices()) {
            writeGroup(out, 10, v.x);
            writeGroup(out, 20, v.y);
        }
        writeGroup(out, 97, 0); // no source boundary objects
        writeGroup(out, 75, 0); // hatch style: normal
        writeGroup(out, 76, 1); // pattern type: predefined
        if (!solid) {
            writeGroup(out, 52, hatch.patternAngle() * 180.0 / M_PI);
            writeGroup(out, 41, hatch.patternScale());
            writeGroup(out, 77, 0); // not double
            const auto& lines = hatchPatternLines(hatch.pattern());
            writeGroup(out, 78, static_cast<int>(lines.size()));
            for (const HatchPatternLine& line : lines) {
                // DXF wants final values: angles include the hatch angle, and
                // base/offset are scaled, with the offset rotated into place.
                const double angle = line.angleDeg * M_PI / 180.0 + hatch.patternAngle();
                const Point2D base = rotateAround(line.base * hatch.patternScale(), Point2D(), hatch.patternAngle());
                const Point2D offset = rotateAround(line.offset * hatch.patternScale(), Point2D(), angle);
                writeGroup(out, 53, angle * 180.0 / M_PI);
                writeGroup(out, 43, base.x);
                writeGroup(out, 44, base.y);
                writeGroup(out, 45, offset.x);
                writeGroup(out, 46, offset.y);
                writeGroup(out, 79, static_cast<int>(line.dashes.size()));
                for (double dash : line.dashes) writeGroup(out, 49, dash * hatch.patternScale());
            }
        }
        writeGroup(out, 98, 0); // no seed points
        // Simplified GRADIENT marker: real DXF stores a full gradient
        // definition (450-470 range including angle/named presets); this
        // just carries the second color, enough to round-trip within KumCAD.
        // The base color (this fill's "color1") already goes out via
        // writeCommon's 62/420 above, since GradientCommand sets it as this
        // entity's normal color override.
        if (const auto& c2 = hatch.gradientColor2()) {
            writeGroup(out, 450, 1);
            writeGroup(out, 421, trueColor(*c2));
        }
        break;
    }
    case EntityType::Insert: {
        const auto& insert = static_cast<const InsertEntity&>(e);
        writeGroup(out, 0, "INSERT");
        writeCommon(out, document, e);
        if (!insert.attributes().empty()) writeGroup(out, 66, 1); // ATTRIBs follow
        writeGroup(out, 2, insert.blockName());
        writeGroup(out, 10, insert.position().x);
        writeGroup(out, 20, insert.position().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 41, insert.scaleFactor());
        writeGroup(out, 42, insert.scaleFactor());
        writeGroup(out, 43, insert.scaleFactor());
        writeGroup(out, 50, insert.rotation() * 180.0 / M_PI);
        if (!insert.attributes().empty()) {
            // Resolved attribute values, placed like the block's ATTDEFs
            // transformed by the insert.
            for (const auto& [tag, value] : insert.attributes()) {
                const AttDefEntity* def = nullptr;
                for (const auto& child : insert.block()->entities) {
                    if (child->type() == EntityType::AttDef &&
                        static_cast<const AttDefEntity&>(*child).tag() == tag) {
                        def = static_cast<const AttDefEntity*>(child.get());
                        break;
                    }
                }
                Point2D pos = insert.position();
                double height = 2.5;
                double rotation = insert.rotation();
                if (def) {
                    pos = insert.position() +
                          rotateAround(def->position() * insert.scaleFactor(), Point2D(), insert.rotation());
                    height = def->height() * insert.scaleFactor();
                    rotation = def->rotation() + insert.rotation();
                }
                writeGroup(out, 0, "ATTRIB");
                writeGroup(out, 8, layerName(document, e.layer()));
                writeGroup(out, 10, pos.x);
                writeGroup(out, 20, pos.y);
                writeGroup(out, 30, 0.0);
                writeGroup(out, 40, height);
                writeGroup(out, 1, value);
                writeGroup(out, 50, rotation * 180.0 / M_PI);
                writeGroup(out, 2, tag);
                writeGroup(out, 70, 0);
            }
            writeGroup(out, 0, "SEQEND");
            writeGroup(out, 8, layerName(document, e.layer()));
        }
        break;
    }
    case EntityType::Point: {
        const auto& point = static_cast<const PointEntity&>(e);
        writeGroup(out, 0, "POINT");
        writeCommon(out, document, e);
        writeGroup(out, 10, point.position().x);
        writeGroup(out, 20, point.position().y);
        writeGroup(out, 30, 0.0);
        break;
    }
    case EntityType::ConstructionLine: {
        const auto& cl = static_cast<const ConstructionLineEntity&>(e);
        writeGroup(out, 0, cl.isRay() ? "RAY" : "XLINE");
        writeCommon(out, document, e);
        writeGroup(out, 10, cl.basePoint().x);
        writeGroup(out, 20, cl.basePoint().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 11, cl.direction().x); // unit direction vector
        writeGroup(out, 21, cl.direction().y);
        writeGroup(out, 31, 0.0);
        break;
    }
    case EntityType::AttDef: {
        const auto& attdef = static_cast<const AttDefEntity&>(e);
        writeGroup(out, 0, "ATTDEF");
        writeCommon(out, document, e);
        writeGroup(out, 10, attdef.position().x);
        writeGroup(out, 20, attdef.position().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 40, attdef.height());
        writeGroup(out, 1, attdef.defaultValue());
        writeGroup(out, 50, attdef.rotation() * 180.0 / M_PI);
        writeGroup(out, 3, attdef.prompt());
        writeGroup(out, 2, attdef.tag());
        writeGroup(out, 70, 0);
        break;
    }
    }
}

} // namespace

bool writeDxf(const Document& document, const std::string& path, std::string* errorOut) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (errorOut) *errorOut = "Could not open file for writing";
        return false;
    }
    out.setf(std::ios::fixed);
    out.precision(6);

    writeGroup(out, 0, "SECTION");
    writeGroup(out, 2, "HEADER");
    writeGroup(out, 9, "$ACADVER");
    writeGroup(out, 1, "AC1015");
    writeGroup(out, 9, "$LTSCALE");
    writeGroup(out, 40, document.lineTypeScale());
    writeGroup(out, 9, "$TEXTSTYLE");
    writeGroup(out, 7, document.currentTextStyleName());
    writeGroup(out, 9, "$DIMSTYLE");
    writeGroup(out, 2, document.currentDimStyleName());
    writeGroup(out, 9, "$DIMTXT");
    writeGroup(out, 40, document.dimStyle().textHeight);
    writeGroup(out, 9, "$DIMASZ");
    writeGroup(out, 40, document.dimStyle().arrowSize);
    writeGroup(out, 9, "$DIMDEC");
    writeGroup(out, 70, document.dimStyle().decimals);
    writeGroup(out, 9, "$PDMODE");
    writeGroup(out, 70, document.pointMode());
    writeGroup(out, 9, "$PDSIZE");
    writeGroup(out, 40, document.pointSize());
    writeGroup(out, 9, "$KUMCAD_ANNOSCALE"); // this codebase's simplified CANNOSCALE
    writeGroup(out, 40, document.annotationScale());
    if (const auto& geo = document.geoLocation()) {
        writeGroup(out, 9, "$KUMCAD_GEOLOCATION"); // this codebase's simplified GEOGRAPHICLOCATION
        writeGroup(out, 10, geo->designPoint.x);
        writeGroup(out, 20, geo->designPoint.y);
        writeGroup(out, 40, geo->latitude);
        writeGroup(out, 41, geo->longitude);
        writeGroup(out, 50, geo->northRotation * 180.0 / M_PI);
    }
    writeGroup(out, 0, "ENDSEC");

    writeGroup(out, 0, "SECTION");
    writeGroup(out, 2, "TABLES");
    writeGroup(out, 0, "TABLE");
    writeGroup(out, 2, "LTYPE");
    writeGroup(out, 70, static_cast<int>(allLineTypes().size()));
    for (LineType type : allLineTypes()) {
        const auto& pattern = lineTypePattern(type);
        double patternLength = 0.0;
        for (double element : pattern) patternLength += std::abs(element);
        writeGroup(out, 0, "LTYPE");
        writeGroup(out, 2, lineTypeName(type));
        writeGroup(out, 70, 0);
        writeGroup(out, 3, lineTypeName(type));
        writeGroup(out, 72, 65); // alignment: always 'A'
        writeGroup(out, 73, static_cast<int>(pattern.size()));
        writeGroup(out, 40, patternLength);
        for (double element : pattern) {
            writeGroup(out, 49, element);
            writeGroup(out, 74, 0); // simple element (no embedded shape/text)
        }
    }
    writeGroup(out, 0, "ENDTAB");
    writeGroup(out, 0, "TABLE");
    writeGroup(out, 2, "LAYER");
    writeGroup(out, 70, static_cast<int>(document.layers().size()));
    for (const Layer& layer : document.layers()) {
        writeGroup(out, 0, "LAYER");
        writeGroup(out, 2, layer.name);
        writeGroup(out, 70, layer.locked ? 4 : 0); // bit 2 = frozen/locked flag
        // ACI (62) carries the nearest indexed color for readers that ignore
        // true color, with a negative sign meaning the layer is off; 420
        // carries the exact RGB for readers that support it.
        const int aci = colorToAci(layer.color);
        writeGroup(out, 62, layer.visible ? aci : -aci);
        writeGroup(out, 420, trueColor(layer.color));
        writeGroup(out, 6, lineTypeName(layer.linetype));
        writeGroup(out, 370, static_cast<int>(std::lround(layer.lineweight * 100.0)));
    }
    writeGroup(out, 0, "ENDTAB");
    writeGroup(out, 0, "TABLE");
    writeGroup(out, 2, "STYLE");
    writeGroup(out, 70, static_cast<int>(document.textStyles().size()));
    for (const TextStyle& style : document.textStyles()) {
        writeGroup(out, 0, "STYLE");
        writeGroup(out, 2, style.name);
        writeGroup(out, 70, 0);
        writeGroup(out, 40, style.fixedHeight);
        writeGroup(out, 41, style.widthFactor);
        writeGroup(out, 50, style.obliqueDeg);
        writeGroup(out, 71, 0);
        writeGroup(out, 42, 2.5); // last-used height, required by the spec
        writeGroup(out, 3, style.font);
        writeGroup(out, 4, "");
        if (style.annotative) writeGroup(out, 290, 1); // this codebase's simplified ANNOTATIVE flag
    }
    writeGroup(out, 0, "ENDTAB");
    writeGroup(out, 0, "TABLE");
    writeGroup(out, 2, "DIMSTYLE");
    writeGroup(out, 70, static_cast<int>(document.dimStyles().size()));
    for (const NamedDimStyle& style : document.dimStyles()) {
        writeGroup(out, 0, "DIMSTYLE");
        writeGroup(out, 2, style.name);
        writeGroup(out, 70, 0);
        writeGroup(out, 140, style.style.textHeight);  // DIMTXT
        writeGroup(out, 41, style.style.arrowSize);    // DIMASZ
        writeGroup(out, 271, style.style.decimals);    // DIMDEC
    }
    writeGroup(out, 0, "ENDTAB");
    writeGroup(out, 0, "ENDSEC");

    bool anyPaperContent = document.layouts().size() > 1;
    for (const Layout& layout : document.layouts()) {
        anyPaperContent = anyPaperContent || !layout.viewports.empty() || !layout.entityIds.empty();
    }

    if (!document.blocks().empty() || anyPaperContent) {
        writeGroup(out, 0, "SECTION");
        writeGroup(out, 2, "BLOCKS");
        for (const auto& block : document.blocks()) {
            writeGroup(out, 0, "BLOCK");
            writeGroup(out, 8, "0");
            writeGroup(out, 2, block->name);
            // Xrefs carry flag bits 4|32 and the file path in group 1. Unlike
            // AutoCAD we also write the cached snapshot inline, so the
            // drawing stays viewable when the referenced file is missing.
            writeGroup(out, 70, block->isXref() ? 36 : 0);
            writeGroup(out, 10, 0.0); // child geometry is stored base-point-relative
            writeGroup(out, 20, 0.0);
            writeGroup(out, 30, 0.0);
            writeGroup(out, 3, block->name);
            if (block->isXref()) writeGroup(out, 1, block->xrefPath);
            // Simplified dynamic-block linear parameter: eight group-40
            // reals (base.x/y, end.x/y, frameMin.x/y, frameMax.x/y) rather
            // than a real BLOCK's normal fields, safely ignorable by any
            // reader that doesn't expect them there (round-trips within
            // KumCAD; see DynamicLinearParameter).
            if (block->dynamicParam) {
                const auto& dp = *block->dynamicParam;
                for (double v : {dp.basePoint.x, dp.basePoint.y, dp.endPoint.x, dp.endPoint.y, dp.frameMin.x,
                                 dp.frameMin.y, dp.frameMax.x, dp.frameMax.y}) {
                    writeGroup(out, 40, v);
                }
            }
            for (const auto& child : block->entities) writeEntity(out, document, *child);
            writeGroup(out, 0, "ENDBLK");
            writeGroup(out, 8, "0");
        }
        if (anyPaperContent) {
            // One paper-space block per layout ("*Paper_Space", then
            // "*Paper_Space0", "*Paper_Space1", ...), holding the layout's
            // VIEWPORT entities (sheet placement in 10/20 + 40/41, the model
            // view in 12/22 + 45) followed by the entities drawn directly on
            // the sheet.
            for (std::size_t li = 0; li < document.layouts().size(); ++li) {
                const Layout& layout = document.layouts()[li];
                const std::string blockName =
                    li == 0 ? "*Paper_Space" : "*Paper_Space" + std::to_string(li - 1);
                writeGroup(out, 0, "BLOCK");
                writeGroup(out, 8, "0");
                writeGroup(out, 2, blockName);
                writeGroup(out, 70, 0);
                writeGroup(out, 10, 0.0);
                writeGroup(out, 20, 0.0);
                writeGroup(out, 30, 0.0);
                writeGroup(out, 3, blockName);
                for (const Viewport& vp : layout.viewports) {
                    if (vp.viewScale < 1e-12) continue;
                    writeGroup(out, 0, "VIEWPORT");
                    writeGroup(out, 8, "0");
                    writeGroup(out, 10, vp.paperCenter.x);
                    writeGroup(out, 20, vp.paperCenter.y);
                    writeGroup(out, 30, 0.0);
                    writeGroup(out, 40, vp.paperWidth);
                    writeGroup(out, 41, vp.paperHeight);
                    writeGroup(out, 68, 1); // on
                    writeGroup(out, 12, vp.modelCenter.x);
                    writeGroup(out, 22, vp.modelCenter.y);
                    writeGroup(out, 45, vp.paperHeight / vp.viewScale); // view height, model units
                }
                for (const Entity* e : document.paperEntities(static_cast<int>(li))) {
                    writeEntity(out, document, *e);
                }
                writeGroup(out, 0, "ENDBLK");
                writeGroup(out, 8, "0");
            }
        }
        writeGroup(out, 0, "ENDSEC");
    }

    writeGroup(out, 0, "SECTION");
    writeGroup(out, 2, "ENTITIES");
    for (const Entity* e : document.entities()) writeEntity(out, document, *e);
    writeGroup(out, 0, "ENDSEC");

    // Minimal LAYOUT objects carrying each layout's tab name and sheet size,
    // in layouts() order (paired with the paper-space blocks by position).
    writeGroup(out, 0, "SECTION");
    writeGroup(out, 2, "OBJECTS");
    for (const Layout& layout : document.layouts()) {
        writeGroup(out, 0, "LAYOUT");
        writeGroup(out, 1, layout.name);
        writeGroup(out, 44, layout.paperWidth);
        writeGroup(out, 45, layout.paperHeight);
    }
    writeGroup(out, 0, "ENDSEC");
    writeGroup(out, 0, "EOF");

    if (!out) {
        if (errorOut) *errorOut = "Write failed (disk full or permissions?)";
        return false;
    }
    return true;
}

} // namespace lcad
