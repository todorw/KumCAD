#include "core/io/DwgWriter.h"

#ifdef LCAD_HAS_DWG

#include "core/geometry/Arc.h"
#include "core/geometry/AttDef.h"
#include "core/geometry/Circle.h"
#include "core/geometry/ConstructionLine.h"
#include "core/geometry/PointEnt.h"
#include "core/geometry/Dimension.h"
#include "core/geometry/Ellipse.h"
#include "core/geometry/Hatch.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Leader.h"
#include "core/geometry/Line.h"
#include "core/geometry/MLeader.h"
#include "core/geometry/MText.h"
#include "core/geometry/Polyline.h"
#include "core/geometry/Spline.h"
#include "core/geometry/Table.h"
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
            Dwg_Entity_INSERT* made =
                dwg_add_INSERT(blkhdr, &p, insert.blockName().c_str(), insert.scaleFactor(), insert.scaleFactor(),
                               insert.scaleFactor(), insert.rotation());
            applyCommon(made, e);
            if (made && insert.block()) {
                // Walk the block's own ATTDEFs (not insert.attributes()) so
                // each ATTRIB lands at its definition's transformed position
                // and height, the same placement instantiate() uses to
                // render resolved attribute text.
                bool anyAttrib = false;
                for (const auto& child : insert.block()->entities) {
                    if (child->type() != EntityType::AttDef) continue;
                    const auto& attdef = static_cast<const AttDefEntity&>(*child);
                    const std::string* value = insert.attributeValue(attdef.tag());
                    const std::string& text = value ? *value : attdef.defaultValue();
                    if (text.empty()) continue;
                    const Point2D world =
                        insert.position() +
                        rotateAround(attdef.position() * insert.scaleFactor(), Point2D(), insert.rotation());
                    const dwg_point_3d ap = pt3(world);
                    if (dwg_add_ATTRIB(made, attdef.height() * insert.scaleFactor(), 0, &ap, attdef.tag().c_str(),
                                       text.c_str())) {
                        anyAttrib = true;
                    } else {
                        ++skipped;
                    }
                }
                // The attrib sub-entity chain (like a POLYLINE's vertices)
                // needs its own SEQEND to close it out, or readers see the
                // "attributes follow" flag with no attributes behind it.
                if (anyAttrib) dwg_add_SEQEND(reinterpret_cast<dwg_ent_generic*>(made));
            }
            return true;
        }
        case EntityType::AttDef: {
            const auto& attdef = static_cast<const AttDefEntity&>(e);
            const dwg_point_3d p = pt3(attdef.position());
            Dwg_Entity_ATTDEF* made = dwg_add_ATTDEF(blkhdr, attdef.height(), /*mode=*/0, attdef.prompt().c_str(),
                                                     &p, attdef.tag().c_str(), attdef.defaultValue().c_str());
            if (made) {
                made->rotation = attdef.rotation();
                reinterpret_cast<dwg_ent_generic*>(made)->parent->entmode = 2;
            }
            applyCommon(made, e);
            return true;
        }
        case EntityType::Point: {
            const dwg_point_3d p = pt3(static_cast<const PointEntity&>(e).position());
            applyCommon(dwg_add_POINT(blkhdr, &p), e);
            return true;
        }
        case EntityType::ConstructionLine: {
            const auto& cl = static_cast<const ConstructionLineEntity&>(e);
            const dwg_point_3d p = pt3(cl.basePoint());
            const dwg_point_3d v = pt3(cl.direction());
            applyCommon(cl.isRay() ? static_cast<void*>(dwg_add_RAY(blkhdr, &p, &v))
                                   : static_cast<void*>(dwg_add_XLINE(blkhdr, &p, &v)),
                        e);
            return true;
        }
        case EntityType::Leader: {
            const auto& leader = static_cast<const LeaderEntity&>(e);
            if (leader.points().size() < 2) return false;
            std::vector<dwg_point_3d> pts;
            pts.reserve(leader.points().size());
            for (const Point2D& p : leader.points()) pts.push_back(pt3(p));
            // Experimental API (LibreDWG's own annotation, not ours); the
            // annotation MTEXT is a separate Document entity already
            // written via the MText case above, so no association is passed.
            applyCommon(dwg_add_LEADER(blkhdr, static_cast<unsigned>(pts.size()), pts.data(), nullptr, 0), e);
            return true;
        }
        case EntityType::MLeader: {
            // No dedicated MULTILEADER add API; each leg goes out as its own
            // LEADER converging on the shared landing point.
            const auto& mleader = static_cast<const MLeaderEntity&>(e);
            bool any = false;
            for (const auto& leg : mleader.legs()) {
                if (leg.empty()) continue;
                std::vector<dwg_point_3d> pts;
                pts.reserve(leg.size() + 1);
                for (const Point2D& p : leg) pts.push_back(pt3(p));
                pts.push_back(pt3(mleader.landing()));
                applyCommon(dwg_add_LEADER(blkhdr, static_cast<unsigned>(pts.size()), pts.data(), nullptr, 0), e);
                any = true;
            }
            return any;
        }
        case EntityType::Hatch: {
            // HATCH's add API derives its boundary path from a real geometry
            // object (line/polyline/circle/ellipse/spline), so a boundary
            // LWPOLYLINE is created first and referenced non-associatively;
            // it stays in the drawing as visible geometry (the same outcome
            // AutoCAD leaves behind with "retain boundaries" on), but the
            // HATCH itself now carries a real fill pattern instead of being
            // dropped to outline-only.
            const auto& hatch = static_cast<const HatchEntity&>(e);
            if (hatch.vertices().size() < 3) return false;
            std::vector<dwg_point_2d> pts;
            pts.reserve(hatch.vertices().size());
            for (const Point2D& v : hatch.vertices()) pts.push_back(dwg_point_2d{v.x, v.y});
            Dwg_Entity_LWPOLYLINE* boundary = dwg_add_LWPOLYLINE(blkhdr, static_cast<int>(pts.size()), pts.data());
            if (!boundary) {
                ++skipped;
                return true;
            }
            boundary->flag |= 512; // closed
            applyCommon(boundary, e);

            int err = 0;
            Dwg_Object* boundaryObj = dwg_ent_generic_to_object(boundary, &err);
            if (err || !boundaryObj) return true; // boundary alone still reads as the hatch shape

            const bool solid = !hatch.isGradient() && hatch.pattern() == HatchPattern::Solid;
            const std::string name = solid ? "SOLID" : hatchPatternName(hatch.pattern());
            const Dwg_Object* pathobjs[1] = {boundaryObj};
            Dwg_Entity_HATCH* made =
                dwg_add_HATCH(blkhdr, /*pattern_type=*/1 /* user-defined/predefined */, name.c_str(),
                              /*is_associative=*/false, 1, pathobjs);
            if (made) {
                made->is_solid_fill = solid;
                made->angle = hatch.patternAngle();
                made->scale_spacing = hatch.patternScale();
                applyCommon(made, e);
            }
            return true;
        }
        case EntityType::Table: {
            // No TABLE add API: exploded into grid lines (LWPOLYLINE per
            // row/column boundary) plus one TEXT per non-empty cell, the
            // same degrade-to-visible-geometry approach as bulged polylines.
            const auto& table = static_cast<const TableEntity&>(e);
            const Point2D& pos = table.position();
            const double totalW = table.totalWidth();
            const double totalH = table.totalHeight();
            const auto hLine = [&](double y) {
                std::vector<dwg_point_2d> pts{{pos.x, y}, {pos.x + totalW, y}};
                applyCommon(dwg_add_LWPOLYLINE(blkhdr, 2, pts.data()), e);
            };
            const auto vLine = [&](double x) {
                std::vector<dwg_point_2d> pts{{x, pos.y}, {x, pos.y - totalH}};
                applyCommon(dwg_add_LWPOLYLINE(blkhdr, 2, pts.data()), e);
            };
            double y = pos.y;
            hLine(y);
            for (double h : table.rowHeights()) {
                y -= h;
                hLine(y);
            }
            double x = pos.x;
            vLine(x);
            for (double w : table.colWidths()) {
                x += w;
                vLine(x);
            }
            for (int r = 0; r < table.rows(); ++r) {
                for (int c = 0; c < table.cols(); ++c) {
                    const std::string& text = table.cellText(r, c);
                    if (text.empty()) continue;
                    const BoundingBox cell = table.cellRect(r, c);
                    const dwg_point_3d p = pt3(Point2D(cell.min.x + 0.2 * table.textHeight(), cell.max.y - 0.8 * table.textHeight()));
                    applyCommon(dwg_add_TEXT(blkhdr, text.c_str(), &p, table.textHeight()), e);
                }
            }
            return true;
        }
        default:
            return false;
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

    // Block definitions first so INSERTs can reference them by name. A
    // dedicated BLOCK_HEADER (block table entry) must exist before adding
    // its BLOCK begin-marker and children -- dwg_add_BLOCK() alone only
    // inserts a BLOCK entity into whatever header it's given (previously
    // this passed mspace directly, which silently merged every block's
    // entities into model space instead of defining a separate block).
    for (const auto& block : document.blocks()) {
        Dwg_Object_BLOCK_HEADER* owner = dwg_add_BLOCK_HEADER(&dwg, block->name.c_str());
        if (!owner || !dwg_add_BLOCK(owner, block->name.c_str())) {
            exporter.skipped += static_cast<int>(block->entities.size());
            continue;
        }
        // ATTDEFs must be converted before anything else in a fresh block:
        // dwg_add_ATTDEF doesn't splice into an already-non-empty
        // owned-entity chain (verified against LibreDWG 0.13.3 -- it's
        // silently dropped from get_next_owned_entity()'s walk on read-back
        // unless it's the very first entity added to the header).
        for (const auto& child : block->entities) {
            if (child->type() == EntityType::AttDef && !exporter.convert(*child, owner)) ++exporter.skipped;
        }
        for (const auto& child : block->entities) {
            if (child->type() != EntityType::AttDef && !exporter.convert(*child, owner)) ++exporter.skipped;
        }
        dwg_add_ENDBLK(owner);
    }

    for (const Entity* e : document.entities()) {
        if (!exporter.convert(*e, exporter.mspace)) ++exporter.skipped;
    }

    // Every layout gets its own paper-space block: the first reuses the
    // default paper space dwg_add_Document() already set up, and later ones
    // get a fresh BLOCK_HEADER plus a LAYOUT object (dwg_add_LAYOUT, which
    // also registers it in the ACAD_LAYOUT dictionary so it shows up as a
    // tab) -- dwg_add_BLOCK_HEADER's general table-entry API turns out to
    // work fine here even though LibreDWG has no dedicated
    // "add another paper space" call.
    for (std::size_t i = 0; i < document.layouts().size(); ++i) {
        const Layout& layout = document.layouts()[i];
        Dwg_Object_BLOCK_HEADER* pspace = nullptr;
        if (i == 0) {
            if (Dwg_Object* pspaceObj = dwg_paper_space_object(&dwg)) {
                pspace = pspaceObj->tio.object->tio.BLOCK_HEADER;
            }
        } else {
            const std::string blockName = "*Paper_Space" + std::to_string(i);
            pspace = dwg_add_BLOCK_HEADER(&dwg, blockName.c_str());
            if (pspace) {
                int err = 0;
                if (Dwg_Object* pspaceObj = dwg_obj_generic_to_object(pspace, &err); !err && pspaceObj) {
                    dwg_add_LAYOUT(pspaceObj, layout.name.c_str(), "");
                }
            }
        }
        if (!pspace) {
            exporter.skipped += static_cast<int>(layout.entityIds.size());
            continue;
        }
        for (const Viewport& vp : layout.viewports) {
            if (vp.viewScale < 1e-12) continue;
            Dwg_Entity_VIEWPORT* made = dwg_add_VIEWPORT(pspace, "*Active");
            if (made) {
                made->center.x = vp.paperCenter.x;
                made->center.y = vp.paperCenter.y;
                made->center.z = 0.0;
                made->width = vp.paperWidth;
                made->height = vp.paperHeight;
                made->VIEWCTR.x = vp.modelCenter.x;
                made->VIEWCTR.y = vp.modelCenter.y;
                made->VIEWSIZE = vp.paperHeight / vp.viewScale;
                made->on_off = 1;
            } else {
                ++exporter.skipped;
            }
        }
        for (EntityId id : layout.entityIds) {
            const Entity* e = document.findEntity(id);
            if (!e || !exporter.convert(*e, pspace)) ++exporter.skipped;
        }
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
