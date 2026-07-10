#include "core/io/DxfWriter.h"

#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
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
        writeGroup(out, 10, dim.linePoint().x); // definition point on the dimension line
        writeGroup(out, 20, dim.linePoint().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 13, dim.point1().x); // first extension line origin
        writeGroup(out, 23, dim.point1().y);
        writeGroup(out, 33, 0.0);
        writeGroup(out, 14, dim.point2().x); // second extension line origin
        writeGroup(out, 24, dim.point2().y);
        writeGroup(out, 34, 0.0);
        // Type: 0 = rotated (linear), 1 = aligned; bit 32 = block-reference
        // flag that AutoCAD always sets.
        writeGroup(out, 70, dim.aligned() ? 33 : 32);
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
        break;
    }
    case EntityType::Hatch: {
        const auto& hatch = static_cast<const HatchEntity&>(e);
        writeGroup(out, 0, "HATCH");
        writeCommon(out, document, e);
        writeGroup(out, 2, "SOLID");
        writeGroup(out, 70, 1); // solid fill
        writeGroup(out, 71, 0); // non-associative
        writeGroup(out, 91, 1); // one boundary path
        writeGroup(out, 92, 2); // path type: polyline
        writeGroup(out, 72, 0); // no bulges
        writeGroup(out, 73, 1); // closed
        writeGroup(out, 93, static_cast<int>(hatch.vertices().size()));
        for (const Point2D& v : hatch.vertices()) {
            writeGroup(out, 10, v.x);
            writeGroup(out, 20, v.y);
        }
        writeGroup(out, 97, 0); // no source boundary objects
        writeGroup(out, 75, 0); // hatch style: normal
        writeGroup(out, 76, 1); // pattern type: predefined
        writeGroup(out, 98, 0); // no seed points
        break;
    }
    case EntityType::Insert: {
        const auto& insert = static_cast<const InsertEntity&>(e);
        writeGroup(out, 0, "INSERT");
        writeCommon(out, document, e);
        writeGroup(out, 2, insert.blockName());
        writeGroup(out, 10, insert.position().x);
        writeGroup(out, 20, insert.position().y);
        writeGroup(out, 30, 0.0);
        writeGroup(out, 41, insert.scaleFactor());
        writeGroup(out, 42, insert.scaleFactor());
        writeGroup(out, 43, insert.scaleFactor());
        writeGroup(out, 50, insert.rotation() * 180.0 / M_PI);
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
    }
    writeGroup(out, 0, "ENDTAB");
    writeGroup(out, 0, "ENDSEC");

    if (!document.blocks().empty()) {
        writeGroup(out, 0, "SECTION");
        writeGroup(out, 2, "BLOCKS");
        for (const auto& block : document.blocks()) {
            writeGroup(out, 0, "BLOCK");
            writeGroup(out, 8, "0");
            writeGroup(out, 2, block->name);
            writeGroup(out, 70, 0);
            writeGroup(out, 10, 0.0); // child geometry is stored base-point-relative
            writeGroup(out, 20, 0.0);
            writeGroup(out, 30, 0.0);
            writeGroup(out, 3, block->name);
            for (const auto& child : block->entities) writeEntity(out, document, *child);
            writeGroup(out, 0, "ENDBLK");
            writeGroup(out, 8, "0");
        }
        writeGroup(out, 0, "ENDSEC");
    }

    writeGroup(out, 0, "SECTION");
    writeGroup(out, 2, "ENTITIES");
    for (const Entity* e : document.entities()) writeEntity(out, document, *e);
    writeGroup(out, 0, "ENDSEC");
    writeGroup(out, 0, "EOF");

    if (!out) {
        if (errorOut) *errorOut = "Write failed (disk full or permissions?)";
        return false;
    }
    return true;
}

} // namespace lcad
