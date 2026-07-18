#include "core/pcb/Board3D.h"

#include "core/document/Document.h"
#include "core/geometry/Insert.h"
#include "core/geometry/Track.h"
#include "core/geometry/Via.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>

namespace lcad {

namespace {

// Same local-frame-segment technique Bim.cpp's Wall/Beam already use --
// X along a-b, Y across width, Z up.
gp_Trsf segmentTransform(const Point2D& a, const Point2D& b) {
    const double angle = std::atan2(b.y - a.y, b.x - a.x);
    gp_Trsf rotate;
    rotate.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), angle);
    gp_Trsf translate;
    translate.SetTranslation(gp_Vec(a.x, a.y, 0.0));
    return translate.Multiplied(rotate);
}

TopoDS_Shape buildTrackSegmentShape(const Point2D& a, const Point2D& b, double width, double thickness, double z) {
    const double length = a.distanceTo(b);
    if (length <= 1e-9 || width <= 1e-9 || thickness <= 1e-9) return TopoDS_Shape();
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(0.0, -width / 2.0, z), length, width, thickness).Shape();
    return BRepBuilderAPI_Transform(box, segmentTransform(a, b), true).Shape();
}

} // namespace

Board3DShapes buildBoard3D(const Document& doc, const std::vector<std::pair<double, double>>& boardOutline,
                          const CopperStackup& stackup, const Board3DParams& params) {
    Board3DShapes result;

    if (boardOutline.size() >= 3 && params.boardThickness > 1e-9) {
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& [x, y] : boardOutline) polygon.Add(gp_Pnt(x, y, 0.0));
        polygon.Close();
        if (polygon.IsDone()) {
            BRepBuilderAPI_MakeFace faceBuilder(polygon.Wire());
            if (faceBuilder.IsDone()) {
                result.substrate = BRepPrimAPI_MakePrism(faceBuilder.Face(), gp_Vec(0.0, 0.0, params.boardThickness)).Shape();
            }
        }
    }

    const bool stackupActive = !stackup.layers.empty();
    auto layerZ = [&](LayerId layer) -> double {
        if (!stackupActive) return params.boardThickness;
        const auto it = std::find(stackup.layers.begin(), stackup.layers.end(), layer);
        if (it == stackup.layers.end()) return params.boardThickness;
        const int idx = static_cast<int>(it - stackup.layers.begin());
        const int last = static_cast<int>(stackup.layers.size()) - 1;
        if (last <= 0) return params.boardThickness;
        return params.boardThickness * (1.0 - static_cast<double>(idx) / static_cast<double>(last));
    };

    for (const Entity* e : doc.entities()) {
        if (e->type() == EntityType::Track) {
            const auto* track = static_cast<const TrackEntity*>(e);
            const double z = layerZ(track->layer());
            const auto& verts = track->vertices();
            for (std::size_t i = 0; i + 1 < verts.size(); ++i) {
                const TopoDS_Shape seg = buildTrackSegmentShape(verts[i], verts[i + 1], track->width(), params.copperThickness, z);
                if (!seg.IsNull()) result.copper.push_back(seg);
            }
        } else if (e->type() == EntityType::Via) {
            const auto* via = static_cast<const ViaEntity*>(e);
            double zLo = 0.0, zHi = params.boardThickness + params.copperThickness;
            if (stackupActive && !via->throughHole) {
                const double a = layerZ(via->fromLayer);
                const double b = layerZ(via->toLayer);
                zLo = std::min(a, b);
                zHi = std::max(a, b) + params.copperThickness;
            }
            const double height = zHi - zLo;
            if (height > 1e-9 && via->diameter() > 1e-9) {
                const gp_Ax2 axis(gp_Pnt(via->position().x, via->position().y, zLo), gp_Dir(0, 0, 1));
                result.copper.push_back(BRepPrimAPI_MakeCylinder(axis, via->diameter() / 2.0, height).Shape());
            }
        } else if (e->type() == EntityType::Insert) {
            const auto* insert = static_cast<const InsertEntity*>(e);
            if (!insert->block() || !insert->block()->isFootprint()) continue;
            const double placementZ = layerZ(insert->layer()); // this footprint's own placement side

            for (const auto& padWorld : insert->padWorldPositions()) {
                if (padWorld.pad->width <= 1e-9 || padWorld.pad->height <= 1e-9 || params.copperThickness <= 1e-9) continue;
                // A through-hole pad's annular ring is real copper on BOTH
                // the top and bottom surfaces (the plated hole itself isn't
                // separately modeled); a surface-mount pad exists only on
                // its own footprint's placement side (see Stackup.h/this
                // header's own comment on the drillDiameter-derived split).
                const bool throughHole = padWorld.pad->drillDiameter > 1e-9;
                std::vector<double> zs = throughHole ? std::vector<double>{0.0, params.boardThickness}
                                                     : std::vector<double>{placementZ};
                for (double z : zs) {
                    TopoDS_Shape padShape;
                    if (padWorld.pad->shape == PadShape::Round) {
                        const gp_Ax2 axis(gp_Pnt(padWorld.position.x, padWorld.position.y, z), gp_Dir(0, 0, 1));
                        padShape = BRepPrimAPI_MakeCylinder(axis, std::max(padWorld.pad->width, padWorld.pad->height) / 2.0,
                                                           params.copperThickness)
                                     .Shape();
                    } else {
                        padShape = BRepPrimAPI_MakeBox(gp_Pnt(padWorld.position.x - padWorld.pad->width / 2.0,
                                                             padWorld.position.y - padWorld.pad->height / 2.0, z),
                                                      padWorld.pad->width, padWorld.pad->height, params.copperThickness)
                                     .Shape();
                    }
                    if (!padShape.IsNull()) result.copper.push_back(padShape);
                }
            }

            const BoundingBox bbox = insert->boundingBox();
            if (bbox.isValid() && params.componentHeight > 1e-9) {
                const double dx = bbox.max.x - bbox.min.x, dy = bbox.max.y - bbox.min.y;
                if (dx > 1e-9 && dy > 1e-9) {
                    result.components.push_back(
                        BRepPrimAPI_MakeBox(gp_Pnt(bbox.min.x, bbox.min.y, params.boardThickness + params.copperThickness),
                                          dx, dy, params.componentHeight)
                            .Shape());
                }
            }
        }
    }

    return result;
}

} // namespace lcad
