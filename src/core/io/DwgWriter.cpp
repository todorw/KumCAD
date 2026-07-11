#include "core/io/DwgWriter.h"

#ifdef LCAD_HAS_DWG

#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"
#include "core/geometry/MText.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Text.h"
#include "core/io/DxfColors.h"

#include <dwg.h>
#include <dwg_api.h>

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace lcad {

namespace {

dwg_point_3d pt3(const Point2D& p) { return dwg_point_3d{p.x, p.y, 0.0}; }

struct DwgExport {
    const Document& doc;
    Dwg_Data& dwg;
    Dwg_Object_BLOCK_HEADER* mspace = nullptr;
    std::unordered_map<LayerId, BITCODE_RLL> layerHandles;
    int skipped = 0;

    void addLayers() {
        for (const Layer& layer : doc.layers()) {
            if (layer.name == "0") continue; // layer 0 always exists
            Dwg_Object_LAYER* made = dwg_add_LAYER(&dwg, layer.name.c_str());
            if (!made) continue;
            made->color.index = colorToAci(layer.color);
            layerHandles[layer.id] = dwg_obj_generic_handlevalue(made);
        }
    }

    // Applies layer and color override to a freshly added entity, going
    // through its generic Dwg_Object_Entity parent.
    void applyCommon(void* made, const Entity& e) {
        if (!made) {
            ++skipped;
            return;
        }
        auto* common = reinterpret_cast<dwg_ent_generic*>(made)->parent;
        if (!common) return;
        const auto it = layerHandles.find(e.layer());
        if (it != layerHandles.end()) {
            common->layer = dwg_add_handleref(&dwg, 5, it->second, nullptr);
        }
        if (const auto& color = e.colorOverride()) {
            common->color.index = colorToAci(*color);
        }
    }

    // Writes one entity into blkhdr. Returns false when the type (or this
    // particular shape) isn't expressible and was counted as skipped.
    bool convert(const Entity& e, Dwg_Object_BLOCK_HEADER* blkhdr) {
        switch (e.type()) {
        case EntityType::Line: {
            const auto& line = static_cast<const LineEntity&>(e);
            const dwg_point_3d a = pt3(line.start());
            const dwg_point_3d b = pt3(line.end());
            applyCommon(dwg_add_LINE(blkhdr, &a, &b), e);
            return true;
        }
        case EntityType::Circle: {
            const auto& circle = static_cast<const CircleEntity&>(e);
            const dwg_point_3d c = pt3(circle.center());
            applyCommon(dwg_add_CIRCLE(blkhdr, &c, circle.radius()), e);
            return true;
        }
        case EntityType::Arc: {
            const auto& arc = static_cast<const ArcEntity&>(e);
            const dwg_point_3d c = pt3(arc.center());
            applyCommon(dwg_add_ARC(blkhdr, &c, arc.radius(), arc.startAngle(), arc.endAngle()), e);
            return true;
        }
        case EntityType::Polyline: {
            const auto& pl = static_cast<const PolylineEntity&>(e);
            if (pl.vertices().size() < 2) return false;
            if (!pl.hasArcs()) {
                std::vector<dwg_point_2d> pts;
                pts.reserve(pl.vertices().size());
                for (const Point2D& v : pl.vertices()) pts.push_back(dwg_point_2d{v.x, v.y});
                Dwg_Entity_LWPOLYLINE* made =
                    dwg_add_LWPOLYLINE(blkhdr, static_cast<int>(pts.size()), pts.data());
                if (made && pl.closed()) made->flag |= 512; // closed bit
                applyCommon(made, e);
                return true;
            }
            // Bulged polylines go out exploded into lines and arcs -- the add
            // API has no documented bulge path yet.
            pl.forEachSegment([&](const Point2D& a, const Point2D& b, double bulge) {
                if (const auto arc = bulgeToArc(a, b, bulge)) {
                    const double lo = arc->sweep >= 0 ? arc->startAngle : arc->startAngle + arc->sweep;
                    const dwg_point_3d c = pt3(arc->center);
                    applyCommon(dwg_add_ARC(blkhdr, &c, arc->radius, lo, lo + std::abs(arc->sweep)), e);
                } else {
                    const dwg_point_3d p1 = pt3(a);
                    const dwg_point_3d p2 = pt3(b);
                    applyCommon(dwg_add_LINE(blkhdr, &p1, &p2), e);
                }
            });
            return true;
        }
        case EntityType::Text: {
            const auto& text = static_cast<const TextEntity&>(e);
            const dwg_point_3d p = pt3(text.position());
            Dwg_Entity_TEXT* made = dwg_add_TEXT(blkhdr, text.text().c_str(), &p, text.height());
            if (made) made->rotation = text.rotation();
            applyCommon(made, e);
            return true;
        }
        case EntityType::MText: {
            const auto& mtext = static_cast<const MTextEntity&>(e);
            const dwg_point_3d p = pt3(mtext.position());
            Dwg_Entity_MTEXT* made = dwg_add_MTEXT(blkhdr, &p, mtext.width(), mtext.text().c_str());
            if (made) {
                made->text_height = mtext.height();
                made->rect_width = mtext.width();
            }
            applyCommon(made, e);
            return true;
        }
        case EntityType::Ellipse: {
            const auto& ellipse = static_cast<const EllipseEntity&>(e);
            const bool xIsMajor = ellipse.radiusX() >= ellipse.radiusY();
            const double major = xIsMajor ? ellipse.radiusX() : ellipse.radiusY();
            const double minor = xIsMajor ? ellipse.radiusY() : ellipse.radiusX();
            if (major < 1e-12) return false;
            const dwg_point_3d c = pt3(ellipse.center());
            Dwg_Entity_ELLIPSE* made = dwg_add_ELLIPSE(blkhdr, &c, major, minor / major);
            if (made) {
                const double dir = xIsMajor ? ellipse.rotation() : ellipse.rotation() + M_PI / 2;
                made->sm_axis.x = major * std::cos(dir);
                made->sm_axis.y = major * std::sin(dir);
                made->sm_axis.z = 0.0;
            }
            applyCommon(made, e);
            return true;
        }
        case EntityType::Spline: {
            const auto& spline = static_cast<const SplineEntity&>(e);
            const auto& fit = spline.fitPoints();
            if (fit.size() < 2) return false; // control-point-only splines aren't expressible via the add API
            std::vector<dwg_point_3d> pts;
            pts.reserve(fit.size());
            for (const Point2D& p : fit) pts.push_back(pt3(p));
            const Point2D begTan = fit[1] - fit[0];
            const Point2D endTan = fit[fit.size() - 1] - fit[fit.size() - 2];
            const dwg_point_3d t0 = pt3(begTan);
            const dwg_point_3d t1 = pt3(endTan);
            applyCommon(dwg_add_SPLINE(blkhdr, static_cast<int>(pts.size()), pts.data(), &t0, &t1), e);
            return true;
        }
        case EntityType::Dimension: {
            const auto& dim = static_cast<const DimensionEntity&>(e);
            const dwg_point_3d p1 = pt3(dim.point1());
            const dwg_point_3d p2 = pt3(dim.point2());
            const dwg_point_3d lp = pt3(dim.linePoint());
            switch (dim.kind()) {
            case DimensionKind::Linear:
                applyCommon(dwg_add_DIMENSION_LINEAR(blkhdr, &p1, &p2, &lp, 0.0), e);
                return true;
            case DimensionKind::Aligned:
                applyCommon(dwg_add_DIMENSION_ALIGNED(blkhdr, &p1, &p2, &lp), e);
                return true;
            case DimensionKind::Radius:
                applyCommon(dwg_add_DIMENSION_RADIUS(blkhdr, &p1, &p2, 0.0), e);
                return true;
            case DimensionKind::Diameter: {
                const dwg_point_3d far = pt3(dim.point1() * 2.0 - dim.point2());
                applyCommon(dwg_add_DIMENSION_DIAMETER(blkhdr, &p2, &far, 0.0), e);
                return true;
            }
            case DimensionKind::Angular: {
                const dwg_point_3d vertex = pt3(dim.vertex());
                applyCommon(dwg_add_DIMENSION_ANG3PT(blkhdr, &vertex, &p1, &p2, &lp), e);
                return true;
            }
            }
            return false;
        }
        case EntityType::Insert: {
            const auto& insert = static_cast<const InsertEntity&>(e);
            const dwg_point_3d p = pt3(insert.position());
            applyCommon(dwg_add_INSERT(blkhdr, &p, insert.blockName().c_str(), insert.scaleFactor(),
                                       insert.scaleFactor(), insert.scaleFactor(), insert.rotation()),
                        e);
            return true;
        }
        default:
            return false; // hatches, leaders: not expressible via the add API yet
        }
    }
};

} // namespace

bool dwgWriteSupportAvailable() { return true; }

bool writeDwg(const Document& document, const std::string& path, std::string* errorOut, int* skippedOut) {
    // R_2000 is the only DWG version LibreDWG's writer handles well.
    Dwg_Data dwg;
    std::memset(&dwg, 0, sizeof(dwg));
    dwg.header.version = R_2000;
    dwg.header.from_version = R_2000;
    if (dwg_add_Document(&dwg, 0 /* metric */) != 0) {
        if (errorOut) *errorOut = "LibreDWG could not create a new drawing";
        return false;
    }

    DwgExport exporter{document, dwg};
    exporter.addLayers();

    Dwg_Object* mspaceObj = dwg_model_space_object(&dwg);
    if (!mspaceObj) {
        dwg_free(&dwg);
        if (errorOut) *errorOut = "LibreDWG drawing has no model space";
        return false;
    }
    exporter.mspace = mspaceObj->tio.object->tio.BLOCK_HEADER;

    // Block definitions first so INSERTs can reference them by name.
    for (const auto& block : document.blocks()) {
        Dwg_Entity_BLOCK* header = dwg_add_BLOCK(exporter.mspace, block->name.c_str());
        Dwg_Object_BLOCK_HEADER* owner = header ? dwg_entity_owner(header) : nullptr;
        if (!owner) {
            exporter.skipped += static_cast<int>(block->entities.size());
            continue;
        }
        for (const auto& child : block->entities) {
            if (!exporter.convert(*child, owner)) ++exporter.skipped;
        }
        dwg_add_ENDBLK(owner);
    }

    for (const Entity* e : document.entities()) {
        if (!exporter.convert(*e, exporter.mspace)) ++exporter.skipped;
    }
    // Paper space isn't exported; count its entities as skipped so the UI
    // can be honest about it.
    for (std::size_t i = 0; i < document.layouts().size(); ++i) {
        exporter.skipped += static_cast<int>(document.layouts()[i].entityIds.size());
    }

    const int error = dwg_write_file(path.c_str(), &dwg);
    const int skipped = exporter.skipped;
    dwg_free(&dwg);

    if (skippedOut) *skippedOut = skipped;
    if (error >= DWG_ERR_CRITICAL) {
        if (errorOut) {
            *errorOut = "LibreDWG failed to write the DWG (error " + std::to_string(error) +
                        "); save as DXF instead";
        }
        return false;
    }
    return true;
}

} // namespace lcad

#else // !LCAD_HAS_DWG

namespace lcad {

bool dwgWriteSupportAvailable() { return false; }

bool writeDwg(const Document&, const std::string&, std::string* errorOut, int* skippedOut) {
    if (skippedOut) *skippedOut = 0;
    if (errorOut) {
        *errorOut = "This build has no DWG support (LibreDWG was not found at build time); save as DXF instead";
    }
    return false;
}

} // namespace lcad

#endif
