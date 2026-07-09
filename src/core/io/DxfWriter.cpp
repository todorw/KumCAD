#include "core/io/DxfWriter.h"

#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/io/DxfColors.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Text.h"

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
    writeGroup(out, 0, "ENDSEC");

    writeGroup(out, 0, "SECTION");
    writeGroup(out, 2, "TABLES");
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
        writeGroup(out, 6, "CONTINUOUS");
    }
    writeGroup(out, 0, "ENDTAB");
    writeGroup(out, 0, "ENDSEC");

    writeGroup(out, 0, "SECTION");
    writeGroup(out, 2, "ENTITIES");
    for (const Entity* e : document.entities()) {
        const std::string layer = layerName(document, e->layer());
        switch (e->type()) {
        case EntityType::Line: {
            const auto& line = static_cast<const LineEntity&>(*e);
            writeGroup(out, 0, "LINE");
            writeGroup(out, 8, layer);
            writeGroup(out, 10, line.start().x);
            writeGroup(out, 20, line.start().y);
            writeGroup(out, 30, 0.0);
            writeGroup(out, 11, line.end().x);
            writeGroup(out, 21, line.end().y);
            writeGroup(out, 31, 0.0);
            break;
        }
        case EntityType::Circle: {
            const auto& circle = static_cast<const CircleEntity&>(*e);
            writeGroup(out, 0, "CIRCLE");
            writeGroup(out, 8, layer);
            writeGroup(out, 10, circle.center().x);
            writeGroup(out, 20, circle.center().y);
            writeGroup(out, 30, 0.0);
            writeGroup(out, 40, circle.radius());
            break;
        }
        case EntityType::Arc: {
            const auto& arc = static_cast<const ArcEntity&>(*e);
            writeGroup(out, 0, "ARC");
            writeGroup(out, 8, layer);
            writeGroup(out, 10, arc.center().x);
            writeGroup(out, 20, arc.center().y);
            writeGroup(out, 30, 0.0);
            writeGroup(out, 40, arc.radius());
            writeGroup(out, 50, arc.startAngle() * 180.0 / M_PI);
            writeGroup(out, 51, arc.endAngle() * 180.0 / M_PI);
            break;
        }
        case EntityType::Polyline: {
            const auto& pl = static_cast<const PolylineEntity&>(*e);
            writeGroup(out, 0, "LWPOLYLINE");
            writeGroup(out, 8, layer);
            writeGroup(out, 90, static_cast<int>(pl.vertices().size()));
            writeGroup(out, 70, pl.closed() ? 1 : 0);
            for (const Point2D& v : pl.vertices()) {
                writeGroup(out, 10, v.x);
                writeGroup(out, 20, v.y);
            }
            break;
        }
        case EntityType::Ellipse: {
            const auto& ellipse = static_cast<const EllipseEntity&>(*e);
            const bool xIsMajor = ellipse.radiusX() >= ellipse.radiusY();
            const double majorRadius = xIsMajor ? ellipse.radiusX() : ellipse.radiusY();
            const double minorRadius = xIsMajor ? ellipse.radiusY() : ellipse.radiusX();
            const double ratio = majorRadius > 1e-12 ? minorRadius / majorRadius : 1.0;
            // 11/21 is the major axis endpoint relative to the center; its
            // direction encodes the ellipse's rotation.
            const double majorDir = xIsMajor ? ellipse.rotation() : ellipse.rotation() + M_PI / 2;
            writeGroup(out, 0, "ELLIPSE");
            writeGroup(out, 8, layer);
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
        case EntityType::Dimension: {
            const auto& dim = static_cast<const DimensionEntity&>(*e);
            writeGroup(out, 0, "DIMENSION");
            writeGroup(out, 8, layer);
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
            const auto& text = static_cast<const TextEntity&>(*e);
            writeGroup(out, 0, "TEXT");
            writeGroup(out, 8, layer);
            writeGroup(out, 10, text.position().x);
            writeGroup(out, 20, text.position().y);
            writeGroup(out, 30, 0.0);
            writeGroup(out, 40, text.height());
            writeGroup(out, 1, text.text());
            writeGroup(out, 50, text.rotation() * 180.0 / M_PI);
            break;
        }
        }
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
