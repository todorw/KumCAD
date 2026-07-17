#include "core/core3d/Piping.h"

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <cmath>

namespace lcad {

namespace {
gp_Pnt toPnt(const std::array<double, 3>& p) { return gp_Pnt(p[0], p[1], p[2]); }
} // namespace

TopoDS_Shape buildPipeShape(const PipeRun& run) {
    if (run.path.size() < 2 || run.outerRadius <= 1e-9) return TopoDS_Shape();

    TopoDS_Shape result;
    bool any = false;

    auto addPiece = [&](const TopoDS_Shape& piece) {
        if (piece.IsNull()) return;
        if (!any) {
            result = piece;
            any = true;
            return;
        }
        BRepAlgoAPI_Fuse fuse(result, piece);
        if (fuse.IsDone()) result = fuse.Shape();
    };

    for (std::size_t i = 0; i + 1 < run.path.size(); ++i) {
        const gp_Pnt p1 = toPnt(run.path[i]);
        const gp_Pnt p2 = toPnt(run.path[i + 1]);
        const double dx = p2.X() - p1.X(), dy = p2.Y() - p1.Y(), dz = p2.Z() - p1.Z();
        const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length <= 1e-9) continue; // a repeated point -- no segment to build, not a hard error

        const gp_Ax2 axis(p1, gp_Dir(dx, dy, dz));
        addPiece(BRepPrimAPI_MakeCylinder(axis, run.outerRadius, length).Shape());
    }

    // Interior joints only -- the path's two endpoints are open pipe ends,
    // not direction changes that need an auto-fitted elbow.
    for (std::size_t i = 1; i + 1 < run.path.size(); ++i) {
        addPiece(BRepPrimAPI_MakeSphere(toPnt(run.path[i]), run.outerRadius).Shape());
    }

    return any ? result : TopoDS_Shape();
}

} // namespace lcad
