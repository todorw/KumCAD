#include "core/core3d/Bim.h"

#include "core/document/Document.h"
#include "core/geometry/Table.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <fstream>
#include <sstream>

namespace lcad {

namespace {

// The transform that carries the wall's own local frame (X along its
// length, Y across its thickness, Z up) into world space -- openings use
// the exact same transform so offsetAlongWall/sillHeight line up with the
// wall they're cut into.
gp_Trsf wallLocalToWorld(const Wall& wall) {
    const double angle = std::atan2(wall.y2 - wall.y1, wall.x2 - wall.x1);
    gp_Trsf rotate;
    rotate.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), angle);
    gp_Trsf translate;
    translate.SetTranslation(gp_Vec(wall.x1, wall.y1, 0.0));
    return translate.Multiplied(rotate);
}

TopoDS_Shape buildWallShape(const Wall& wall) {
    const double dx = wall.x2 - wall.x1, dy = wall.y2 - wall.y1;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1e-9 || wall.thickness <= 1e-9 || wall.height <= 1e-9) return TopoDS_Shape();

    const TopoDS_Shape box =
        BRepPrimAPI_MakeBox(gp_Pnt(0.0, -wall.thickness / 2.0, 0.0), length, wall.thickness, wall.height).Shape();
    return BRepBuilderAPI_Transform(box, wallLocalToWorld(wall), true).Shape();
}

TopoDS_Shape buildOpeningCutter(const Wall& wall, const Opening& opening) {
    if (opening.width <= 1e-9 || opening.height <= 1e-9) return TopoDS_Shape();
    // Deliberately taller across the wall's thickness than the wall itself
    // (2x), so the cut always fully punches through regardless of floating
    // point tolerance at the wall's own faces.
    const double cutDepth = wall.thickness * 2.0;
    const TopoDS_Shape box =
        BRepPrimAPI_MakeBox(gp_Pnt(opening.offsetAlongWall, -cutDepth / 2.0, opening.sillHeight), opening.width,
                             cutDepth, opening.height)
            .Shape();
    return BRepBuilderAPI_Transform(box, wallLocalToWorld(wall), true).Shape();
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
        out << '#' << id++ << "=IFCWALL(" << wall.x1 << ',' << wall.y1 << ',' << wall.x2 << ',' << wall.y2 << ','
            << wall.height << ',' << wall.thickness << ");\n";
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

        if (name == "IFCWALL" && values.size() == 6) {
            Wall wall;
            wall.x1 = values[0];
            wall.y1 = values[1];
            wall.x2 = values[2];
            wall.y2 = values[3];
            wall.height = values[4];
            wall.thickness = values[5];
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

} // namespace lcad
