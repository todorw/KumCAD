#include "core/io/DwgReader.h"

#ifdef LCAD_HAS_DWG

#include "core/geometry/Arc.h"
#include "core/geometry/Circle.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Line.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Text.h"
#include "core/io/DxfColors.h"

#include <dwg.h>
#include <dwg_api.h>

#include <cmath>
#include <cstring>
#include <unordered_map>

namespace lcad {

namespace {

Color colorFromRgb(unsigned rgb) {
    return Color{static_cast<std::uint8_t>((rgb >> 16) & 0xFF), static_cast<std::uint8_t>((rgb >> 8) & 0xFF),
                 static_cast<std::uint8_t>(rgb & 0xFF)};
}

// Per-entity color override from the DWG CMC/ENC color: ByLayer (256) and
// ByBlock (0) mean "no override"; truecolor (method 0xc3) beats the ACI index.
std::optional<Color> colorOverrideOf(const Dwg_Color& color) {
    if (color.method == 0xc3) return colorFromRgb(color.rgb & 0xFFFFFF);
    if (color.index >= 1 && color.index <= 255) return aciToColor(color.index);
    return std::nullopt;
}

struct DwgImport {
    Document& doc;
    Dwg_Data& dwg;
    std::unordered_map<std::string, LayerId> layerByName;

    LayerId layerFor(const Dwg_Object* obj) {
        int error = 0;
        char* name = dwg_ent_get_layer_name(obj->tio.entity, &error);
        const std::string layerName = (!error && name) ? name : "0";
        const auto it = layerByName.find(layerName);
        if (it != layerByName.end()) return it->second;
        const LayerId id = doc.addLayer(layerName, Color{255, 255, 255});
        layerByName[layerName] = id;
        return id;
    }

    // Converts one DWG entity to our model, or nullptr for unsupported types.
    std::unique_ptr<Entity> convert(Dwg_Object* obj) {
        const EntityId id = doc.reserveEntityId();
        const LayerId layer = layerFor(obj);
        std::unique_ptr<Entity> made;

        switch (obj->fixedtype) {
        case DWG_TYPE_LINE: {
            const auto* line = obj->tio.entity->tio.LINE;
            made = std::make_unique<LineEntity>(id, layer, Point2D(line->start.x, line->start.y),
                                                Point2D(line->end.x, line->end.y));
            break;
        }
        case DWG_TYPE_CIRCLE: {
            const auto* circle = obj->tio.entity->tio.CIRCLE;
            made = std::make_unique<CircleEntity>(id, layer, Point2D(circle->center.x, circle->center.y),
                                                  circle->radius);
            break;
        }
        case DWG_TYPE_ARC: {
            const auto* arc = obj->tio.entity->tio.ARC;
            made = std::make_unique<ArcEntity>(id, layer, Point2D(arc->center.x, arc->center.y), arc->radius,
                                               arc->start_angle, arc->end_angle);
            break;
        }
        case DWG_TYPE_LWPOLYLINE: {
            const auto* pl = obj->tio.entity->tio.LWPOLYLINE;
            if (pl->num_points < 2) break;
            std::vector<Point2D> verts;
            std::vector<double> bulges(pl->num_points, 0.0);
            verts.reserve(pl->num_points);
            for (BITCODE_BL i = 0; i < pl->num_points; ++i) verts.emplace_back(pl->points[i].x, pl->points[i].y);
            for (BITCODE_BL i = 0; i < pl->num_bulges && i < pl->num_points; ++i) bulges[i] = pl->bulges[i];
            made = std::make_unique<PolylineEntity>(id, layer, std::move(verts), std::move(bulges),
                                                    (pl->flag & 512) != 0);
            break;
        }
        case DWG_TYPE_POLYLINE_2D: {
            int error = 0;
            const BITCODE_BL count = dwg_object_polyline_2d_get_numpoints(obj, &error);
            if (error || count < 2) break;
            dwg_point_2d* pts = dwg_object_polyline_2d_get_points(obj, &error);
            if (error || !pts) break;
            std::vector<Point2D> verts;
            verts.reserve(count);
            for (BITCODE_BL i = 0; i < count; ++i) verts.emplace_back(pts[i].x, pts[i].y);
            free(pts);
            const auto* pl = obj->tio.entity->tio.POLYLINE_2D;
            made = std::make_unique<PolylineEntity>(id, layer, std::move(verts), (pl->flag & 1) != 0);
            break;
        }
        case DWG_TYPE_ELLIPSE: {
            const auto* el = obj->tio.entity->tio.ELLIPSE;
            const double majorLen = std::hypot(el->sm_axis.x, el->sm_axis.y);
            if (majorLen < 1e-12) break;
            const double rotation = std::atan2(el->sm_axis.y, el->sm_axis.x);
            made = std::make_unique<EllipseEntity>(id, layer, Point2D(el->center.x, el->center.y), majorLen,
                                                   majorLen * el->axis_ratio, rotation);
            break;
        }
        case DWG_TYPE_SPLINE: {
            const auto* sp = obj->tio.entity->tio.SPLINE;
            std::vector<Point2D> fit;
            for (BITCODE_BS i = 0; i < sp->num_fit_pts; ++i) fit.emplace_back(sp->fit_pts[i].x, sp->fit_pts[i].y);
            const int degree = std::max(1, static_cast<int>(sp->degree));
            if (sp->num_ctrl_pts > static_cast<BITCODE_BL>(degree) &&
                sp->num_knots == sp->num_ctrl_pts + degree + 1) {
                std::vector<Point2D> ctrl;
                std::vector<double> knots;
                for (BITCODE_BL i = 0; i < sp->num_ctrl_pts; ++i) {
                    ctrl.emplace_back(sp->ctrl_pts[i].x, sp->ctrl_pts[i].y);
                }
                for (BITCODE_BL i = 0; i < sp->num_knots; ++i) knots.push_back(sp->knots[i]);
                made = std::make_unique<SplineEntity>(id, layer, degree, std::move(ctrl), std::move(knots),
                                                      std::move(fit));
            } else if (fit.size() >= 2) {
                made = SplineEntity::fromFitPoints(id, layer, std::move(fit));
            }
            break;
        }
        case DWG_TYPE_TEXT: {
            auto* text = obj->tio.entity->tio.TEXT;
            // dynapi handles the r2007+ UTF-16 to UTF-8 conversion for us.
            char* value = nullptr;
            int isNew = 0;
            if (!dwg_dynapi_entity_utf8text(text, "TEXT", "text_value", &value, &isNew, nullptr)) break;
            if (!value || !*value) break;
            made = std::make_unique<TextEntity>(id, layer, Point2D(text->ins_pt.x, text->ins_pt.y),
                                                std::string(value), text->height, text->rotation);
            if (isNew) free(value);
            break;
        }
        case DWG_TYPE_DIMENSION_LINEAR: {
            const auto* dim = obj->tio.entity->tio.DIMENSION_LINEAR;
            made = std::make_unique<DimensionEntity>(id, layer, Point2D(dim->xline1_pt.x, dim->xline1_pt.y),
                                                     Point2D(dim->xline2_pt.x, dim->xline2_pt.y),
                                                     Point2D(dim->def_pt.x, dim->def_pt.y), false);
            break;
        }
        case DWG_TYPE_DIMENSION_ALIGNED: {
            const auto* dim = obj->tio.entity->tio.DIMENSION_ALIGNED;
            made = std::make_unique<DimensionEntity>(id, layer, Point2D(dim->xline1_pt.x, dim->xline1_pt.y),
                                                     Point2D(dim->xline2_pt.x, dim->xline2_pt.y),
                                                     Point2D(dim->def_pt.x, dim->def_pt.y), true);
            break;
        }
        case DWG_TYPE_INSERT: {
            const auto* ins = obj->tio.entity->tio.INSERT;
            int error = 0;
            Dwg_Object* hdrObj = ins->block_header ? dwg_ref_get_object(ins->block_header, &error) : nullptr;
            if (!hdrObj || error) break;
            char* name = dwg_obj_table_get_name(hdrObj, &error);
            if (error || !name) break;
            if (const BlockDefinition* block = doc.findBlock(name)) {
                made = std::make_unique<InsertEntity>(id, layer, block, Point2D(ins->ins_pt.x, ins->ins_pt.y),
                                                      ins->scale.x, ins->rotation);
            }
            break;
        }
        default:
            break;
        }

        if (made) made->setColorOverride(colorOverrideOf(obj->tio.entity->color));
        return made;
    }

    void convertOwnedEntities(Dwg_Object* header, std::vector<std::unique_ptr<Entity>>* blockOut) {
        Dwg_Object* obj = get_first_owned_entity(header);
        while (obj) {
            if (auto made = convert(obj)) {
                if (blockOut) blockOut->push_back(std::move(made));
                else doc.addEntity(std::move(made));
            }
            obj = get_next_owned_entity(header, obj);
        }
    }
};

} // namespace

bool dwgSupportAvailable() {
    return true;
}

bool readDwg(Document& document, const std::string& path, std::string* errorOut) {
    Dwg_Data dwg;
    std::memset(&dwg, 0, sizeof(dwg));
    const int error = dwg_read_file(path.c_str(), &dwg);
    if (error >= DWG_ERR_CRITICAL) {
        if (errorOut) *errorOut = "LibreDWG could not read this DWG file (error " + std::to_string(error) + ")";
        dwg_free(&dwg);
        return false;
    }

    Document fresh;
    DwgImport import{fresh, dwg, {{"0", 0}}};

    // Layer table first, so entities land on layers with the right colors.
    for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
        Dwg_Object* obj = &dwg.object[i];
        if (obj->fixedtype != DWG_TYPE_LAYER || obj->supertype != DWG_SUPERTYPE_OBJECT) continue;
        int err = 0;
        char* name = dwg_obj_table_get_name(obj, &err);
        if (err || !name || !*name) continue;
        if (import.layerByName.count(name)) continue;

        const auto* layerObj = obj->tio.object->tio.LAYER;
        Color color{255, 255, 255};
        if (layerObj->color.method == 0xc3) color = colorFromRgb(layerObj->color.rgb & 0xFFFFFF);
        else if (std::abs(layerObj->color.index) >= 1 && std::abs(layerObj->color.index) <= 255)
            color = aciToColor(std::abs(layerObj->color.index));

        const LayerId layerId = fresh.addLayer(name, color);
        import.layerByName[name] = layerId;
        if (Layer* layer = fresh.findLayer(layerId)) {
            layer->visible = layerObj->color.index >= 0 && !layerObj->frozen;
            layer->locked = layerObj->locked;
        }
    }

    // Block definitions next (skipping the model/paper space pseudo-blocks),
    // so INSERTs can resolve them.
    Dwg_Object* modelSpace = dwg_model_space_object(&dwg);
    for (BITCODE_BL i = 0; i < dwg.num_objects; ++i) {
        Dwg_Object* obj = &dwg.object[i];
        if (obj->fixedtype != DWG_TYPE_BLOCK_HEADER || obj == modelSpace) continue;
        int err = 0;
        char* name = dwg_obj_table_get_name(obj, &err);
        if (err || !name || !*name || *name == '*') continue; // *Model_Space, *Paper_Space, anonymous
        if (fresh.findBlock(name)) continue;
        std::vector<std::unique_ptr<Entity>> children;
        import.convertOwnedEntities(obj, &children);
        if (!children.empty()) fresh.addBlock(name, std::move(children));
    }

    // Model space entities.
    if (modelSpace) import.convertOwnedEntities(modelSpace, nullptr);

    dwg_free(&dwg);
    document = std::move(fresh);
    return true;
}

} // namespace lcad

#else // !LCAD_HAS_DWG

namespace lcad {

bool dwgSupportAvailable() {
    return false;
}

bool readDwg(Document& document, const std::string& path, std::string* errorOut) {
    (void)document;
    (void)path;
    if (errorOut) {
        *errorOut = "This build has no DWG support. Install LibreDWG (https://www.gnu.org/software/libredwg/) "
                    "and rebuild, or convert the file to DXF.";
    }
    return false;
}

} // namespace lcad

#endif
