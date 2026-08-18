#include "core/core3d/Assembly.h"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lcad;
using Catch::Approx;

namespace {
TopoDS_Shape makeBox(double s) {
    return BRepPrimAPI_MakeBox(s, s, s).Shape();
}
} // namespace

TEST_CASE("Assembly Coincident mate places componentB's reference point onto componentA's, anti-parallel", "[core3d][assembly]") {
    Assembly asm_;
    AssemblyComponent a;
    a.name = "Base";
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.name = "Lid";
    b.shape = makeBox(10.0);
    const int idxB = asm_.addComponent(b);

    // Mate the top face of A (z=10, outward normal +Z) to the bottom face
    // of B (z=0, outward normal -Z) -- the classic "stack B on top of A"
    // face mate. Coincident anti-aligns the two reference directions,
    // which is what makes two *outward* face normals point at each other
    // when the faces touch -- since bottom's own outward normal is already
    // -Z, no flip is needed and B should land right-side up.
    Mate mate;
    mate.type = MateType::Coincident;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.ax = 5.0; mate.ay = 5.0; mate.az = 10.0;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0;
    mate.bx = 5.0; mate.by = 5.0; mate.bz = 0.0;
    mate.bdx = 0.0; mate.bdy = 0.0; mate.bdz = -1.0;
    asm_.addMate(mate);

    asm_.solve();

    const gp_Pnt localRefB(5.0, 5.0, 0.0);
    const gp_Trsf& placement = asm_.components()[static_cast<std::size_t>(idxB)].placement;
    const gp_Pnt worldRefB = localRefB.Transformed(placement);
    REQUIRE(worldRefB.X() == Approx(5.0).margin(1e-6));
    REQUIRE(worldRefB.Y() == Approx(5.0).margin(1e-6));
    REQUIRE(worldRefB.Z() == Approx(10.0).margin(1e-6));

    // No flip needed (see comment above) -- B's own local +Z should still
    // point world +Z, i.e. B lands right-side up (a flip would instead put
    // this point at world Z=9, one below the seam, not one above it).
    const gp_Pnt tipWorld = gp_Pnt(5.0, 5.0, 1.0).Transformed(placement);
    REQUIRE(tipWorld.Z() == Approx(11.0).margin(1e-6));
}

TEST_CASE("Assembly Distance mate offsets componentB's reference point along the shared normal", "[core3d][assembly]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(10.0);
    const int idxB = asm_.addComponent(b);

    Mate mate;
    mate.type = MateType::Distance;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.ax = 5.0; mate.ay = 5.0; mate.az = 10.0;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0;
    mate.bx = 5.0; mate.by = 5.0; mate.bz = 0.0;
    mate.bdx = 0.0; mate.bdy = 0.0; mate.bdz = -1.0;
    mate.value = 4.0; // 4 units of air gap above A
    asm_.addMate(mate);

    asm_.solve();

    const gp_Pnt localRefB(5.0, 5.0, 0.0);
    const gp_Pnt worldRefB = localRefB.Transformed(asm_.components()[static_cast<std::size_t>(idxB)].placement);
    REQUIRE(worldRefB.Z() == Approx(14.0).margin(1e-6));
}

TEST_CASE("Assembly Concentric mate aligns componentB's axis parallel (not flipped) to componentA's", "[core3d][assembly]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(4.0);
    const int idxB = asm_.addComponent(b);

    Mate mate;
    mate.type = MateType::Concentric;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.ax = 5.0; mate.ay = 5.0; mate.az = 0.0;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0; // A's axis points +Z
    mate.bx = 2.0; mate.by = 2.0; mate.bz = 0.0;
    mate.bdx = 0.0; mate.bdy = 0.0; mate.bdz = 1.0; // B's axis, also local +Z
    asm_.addMate(mate);

    asm_.solve();

    // Concentric keeps directions parallel (same sense), unlike Coincident's
    // flip -- B's local +Z reference direction should still point +Z in
    // world space, and its reference point should land exactly on A's.
    const gp_Trsf& placement = asm_.components()[static_cast<std::size_t>(idxB)].placement;
    const gp_Pnt worldRefB = gp_Pnt(2.0, 2.0, 0.0).Transformed(placement);
    REQUIRE(worldRefB.X() == Approx(5.0).margin(1e-6));
    REQUIRE(worldRefB.Y() == Approx(5.0).margin(1e-6));
    REQUIRE(worldRefB.Z() == Approx(0.0).margin(1e-6));

    const gp_Pnt tipLocal(2.0, 2.0, 1.0); // one unit up B's local axis from its reference point
    const gp_Pnt tipWorld = tipLocal.Transformed(placement);
    REQUIRE(tipWorld.Z() == Approx(1.0).margin(1e-6)); // still moving in +Z, not flipped to -Z
}

TEST_CASE("Assembly Parallel mate rotates componentB's direction without moving its current position",
         "[core3d][assembly]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(4.0);
    const int idxB = asm_.addComponent(b);
    // Give B some pre-existing world position, with identity rotation, so
    // its own local +X currently points world +X -- unlike every other
    // mate type, Parallel must leave this translation untouched.
    asm_.components()[static_cast<std::size_t>(idxB)].placement.SetTranslationPart(gp_Vec(100.0, 50.0, 25.0));

    Mate mate;
    mate.type = MateType::Parallel;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0; // A's reference direction, world +Z (A is fixed/identity)
    mate.bx = 0.0; mate.by = 0.0; mate.bz = 0.0;     // pivot exactly at B's own placement origin
    mate.bdx = 1.0; mate.bdy = 0.0; mate.bdz = 0.0;  // B's reference direction, local +X
    asm_.addMate(mate);

    asm_.solve();

    const gp_Trsf& placement = asm_.components()[static_cast<std::size_t>(idxB)].placement;
    // Position must be exactly preserved (pivot was at the origin of B's
    // own placement).
    REQUIRE(placement.TranslationPart().X() == Approx(100.0).margin(1e-6));
    REQUIRE(placement.TranslationPart().Y() == Approx(50.0).margin(1e-6));
    REQUIRE(placement.TranslationPart().Z() == Approx(25.0).margin(1e-6));

    // B's local +X should now transform to something parallel to world +Z.
    const gp_Pnt origin = gp_Pnt(0, 0, 0).Transformed(placement);
    const gp_Pnt tip = gp_Pnt(1, 0, 0).Transformed(placement);
    const gp_Vec dir(origin, tip);
    REQUIRE(std::abs(dir.X()) < 1e-6);
    REQUIRE(std::abs(dir.Y()) < 1e-6);
    REQUIRE(std::abs(std::abs(dir.Z()) - 1.0) < 1e-6);
}

TEST_CASE("Assembly Perpendicular mate rotates componentB's direction to the closest perpendicular, "
         "also without moving its current position",
         "[core3d][assembly]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(4.0);
    const int idxB = asm_.addComponent(b);
    asm_.components()[static_cast<std::size_t>(idxB)].placement.SetTranslationPart(gp_Vec(1.0, 2.0, 3.0));

    Mate mate;
    mate.type = MateType::Perpendicular;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0; // A's reference direction, world +Z
    mate.bx = 0.0; mate.by = 0.0; mate.bz = 0.0;
    // B's own current direction is 45 degrees off both X and Z -- the
    // closest perpendicular-to-Z direction is +X, not some arbitrary one.
    mate.bdx = 1.0; mate.bdy = 0.0; mate.bdz = 1.0;
    asm_.addMate(mate);

    asm_.solve();

    const gp_Trsf& placement = asm_.components()[static_cast<std::size_t>(idxB)].placement;
    REQUIRE(placement.TranslationPart().X() == Approx(1.0).margin(1e-6));
    REQUIRE(placement.TranslationPart().Y() == Approx(2.0).margin(1e-6));
    REQUIRE(placement.TranslationPart().Z() == Approx(3.0).margin(1e-6));

    const gp_Pnt origin = gp_Pnt(0, 0, 0).Transformed(placement);
    const gp_Pnt tip = gp_Pnt(1, 0, 1).Transformed(placement);
    const gp_Vec dir(origin, tip);
    const gp_Vec normalized = dir / dir.Magnitude();
    REQUIRE(std::abs(normalized.Z()) < 1e-6); // perpendicular to A's world +Z
    REQUIRE(std::abs(normalized.X()) > 0.99); // the closest perpendicular to (1,0,1) is (+-1,0,0)
}

TEST_CASE("Assembly Perpendicular mate still produces a perpendicular direction when componentB's own "
         "current direction is already exactly parallel to componentA's",
         "[core3d][assembly]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(4.0);
    const int idxB = asm_.addComponent(b);

    Mate mate;
    mate.type = MateType::Perpendicular;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0;
    mate.bx = 0.0; mate.by = 0.0; mate.bz = 0.0;
    mate.bdx = 0.0; mate.bdy = 0.0; mate.bdz = 1.0; // already exactly parallel to A's direction
    asm_.addMate(mate);

    asm_.solve();

    const gp_Trsf& placement = asm_.components()[static_cast<std::size_t>(idxB)].placement;
    const gp_Pnt origin = gp_Pnt(0, 0, 0).Transformed(placement);
    const gp_Pnt tip = gp_Pnt(0, 0, 1).Transformed(placement);
    const gp_Vec dir(origin, tip);
    REQUIRE(std::abs(dir.Z()) < 1e-6); // some perpendicular direction, not necessarily a specific one
}

TEST_CASE("Assembly Angle mate rotates componentB around the shared axis by the given angle", "[core3d][assembly]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(4.0);
    const int idxB = asm_.addComponent(b);

    Mate mate;
    mate.type = MateType::Angle;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.ax = 0.0; mate.ay = 0.0; mate.az = 0.0;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0; // shared axis is world Z through the origin
    mate.bx = 0.0; mate.by = 0.0; mate.bz = 0.0;
    mate.bdx = 0.0; mate.bdy = 0.0; mate.bdz = 1.0;
    mate.value = 90.0; // spin B 90 degrees around that axis
    asm_.addMate(mate);

    asm_.solve();

    // A point 1 unit along B's local +X should end up along world +Y after
    // a 90-degree spin around +Z.
    const gp_Pnt tipWorld = gp_Pnt(1.0, 0.0, 0.0).Transformed(asm_.components()[static_cast<std::size_t>(idxB)].placement);
    REQUIRE(tipWorld.X() == Approx(0.0).margin(1e-6));
    REQUIRE(tipWorld.Y() == Approx(1.0).margin(1e-6));
    REQUIRE(tipWorld.Z() == Approx(0.0).margin(1e-6));
}

TEST_CASE("Assembly solves a mate chain in list order, matching Document3D's own append-order convention", "[core3d][assembly]") {
    Assembly asm_;
    AssemblyComponent base;
    base.shape = makeBox(10.0);
    base.fixed = true;
    const int idxBase = asm_.addComponent(base);

    AssemblyComponent middle;
    middle.shape = makeBox(10.0);
    const int idxMiddle = asm_.addComponent(middle);

    AssemblyComponent top;
    top.shape = makeBox(10.0);
    const int idxTop = asm_.addComponent(top);

    // Each mate is A's top face (outward +Z) to B's bottom face (outward
    // -Z) -- see the first Coincident test's comment for why that's a
    // no-flip stack, which is what lets this chain add up cleanly.
    Mate baseToMiddle;
    baseToMiddle.type = MateType::Coincident;
    baseToMiddle.componentA = idxBase;
    baseToMiddle.componentB = idxMiddle;
    baseToMiddle.az = 10.0;
    baseToMiddle.adz = 1.0;
    baseToMiddle.bz = 0.0;
    baseToMiddle.bdz = -1.0;
    asm_.addMate(baseToMiddle);

    Mate middleToTop;
    middleToTop.type = MateType::Coincident;
    middleToTop.componentA = idxMiddle; // depends on middle already being placed by the mate above
    middleToTop.componentB = idxTop;
    middleToTop.az = 10.0;
    middleToTop.adz = 1.0;
    middleToTop.bz = 0.0;
    middleToTop.bdz = -1.0;
    asm_.addMate(middleToTop);

    asm_.solve();

    const gp_Pnt topOrigin = gp_Pnt(0, 0, 0).Transformed(asm_.components()[static_cast<std::size_t>(idxTop)].placement);
    REQUIRE(topOrigin.Z() == Approx(20.0).margin(1e-6)); // stacked base(0-10) + middle(10-20) + top starts at 20
}

TEST_CASE("analyzeAssemblyDof flags a component that's neither fixed nor mated as unplaced", "[core3d][assembly][dof]") {
    Assembly asm_;
    AssemblyComponent base;
    base.shape = makeBox(10.0);
    base.fixed = true;
    const int idxBase = asm_.addComponent(base);

    AssemblyComponent mated;
    mated.shape = makeBox(10.0);
    const int idxMated = asm_.addComponent(mated);

    AssemblyComponent floating;
    floating.shape = makeBox(5.0);
    const int idxFloating = asm_.addComponent(floating);

    Mate mate;
    mate.type = MateType::Coincident;
    mate.componentA = idxBase;
    mate.componentB = idxMated;
    asm_.addMate(mate);

    const AssemblyDofReport report = analyzeAssemblyDof(asm_);
    REQUIRE(report.unplacedComponentIndices.size() == 1);
    REQUIRE(report.unplacedComponentIndices[0] == idxFloating);
    REQUIRE(report.multiplyMatedComponentIndices.empty());
}

TEST_CASE("analyzeAssemblyDof flags a component mated more than once (later mate silently wins)",
          "[core3d][assembly][dof]") {
    Assembly asm_;
    AssemblyComponent base;
    base.shape = makeBox(10.0);
    base.fixed = true;
    const int idxBase = asm_.addComponent(base);
    AssemblyComponent target;
    target.shape = makeBox(5.0);
    const int idxTarget = asm_.addComponent(target);

    Mate first;
    first.componentA = idxBase;
    first.componentB = idxTarget;
    asm_.addMate(first);
    Mate second;
    second.componentA = idxBase;
    second.componentB = idxTarget; // same target again -- overwrites the first mate's placement
    second.value = 5.0;
    asm_.addMate(second);

    const AssemblyDofReport report = analyzeAssemblyDof(asm_);
    REQUIRE(report.multiplyMatedComponentIndices.size() == 1);
    REQUIRE(report.multiplyMatedComponentIndices[0] == idxTarget);
    REQUIRE(report.unplacedComponentIndices.empty());
}

TEST_CASE("Assembly Tangent mate rotates componentB's axis parallel to componentA's plane and offsets it "
         "by the cylinder radius",
         "[core3d][assembly][tangent]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(2.0);
    const int idxB = asm_.addComponent(b);
    asm_.components()[static_cast<std::size_t>(idxB)].placement.SetTranslationPart(gp_Vec(5.0, 5.0, 5.0));

    Mate mate;
    mate.type = MateType::Tangent;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0; // A's plane: the world XY plane (normal +Z) at Z=0
    mate.bx = 0.0; mate.by = 0.0; mate.bz = 0.0;
    // B's own cylinder axis starts tilted 45 degrees off the plane's normal.
    mate.bdx = 1.0; mate.bdy = 0.0; mate.bdz = 1.0;
    mate.value = 2.5; // cylinder radius
    asm_.addMate(mate);

    asm_.solve();

    const gp_Trsf& placement = asm_.components()[static_cast<std::size_t>(idxB)].placement;
    const gp_Pnt axisPoint = gp_Pnt(0, 0, 0).Transformed(placement);
    const gp_Pnt axisTip = gp_Pnt(1, 0, 1).Transformed(placement);
    const gp_Vec axisDir = (gp_Vec(axisPoint, axisTip)) / axisPoint.Distance(axisTip);

    // The axis is now parallel to the plane (perpendicular to +Z)...
    REQUIRE(std::abs(axisDir.Z()) < 1e-6);
    // ...and its reference point sits exactly `value` above the plane.
    REQUIRE(axisPoint.Z() == Approx(2.5).margin(1e-6));
}

TEST_CASE("Assembly Tangent mate is idempotent: solving twice doesn't drift the offset", "[core3d][assembly][tangent]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);
    AssemblyComponent b;
    b.shape = makeBox(2.0);
    const int idxB = asm_.addComponent(b);

    Mate mate;
    mate.type = MateType::Tangent;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.adz = 1.0;
    mate.bdx = 1.0;
    mate.value = 3.0;
    asm_.addMate(mate);

    asm_.solve();
    const double firstZ = gp_Pnt(0, 0, 0).Transformed(asm_.components()[static_cast<std::size_t>(idxB)].placement).Z();
    asm_.solve();
    const double secondZ = gp_Pnt(0, 0, 0).Transformed(asm_.components()[static_cast<std::size_t>(idxB)].placement).Z();
    REQUIRE(firstZ == Approx(3.0).margin(1e-6));
    REQUIRE(secondZ == Approx(3.0).margin(1e-6));
}

TEST_CASE("Assembly AxisTangentExternal mate places two parallel axes radiusA+radiusB apart",
          "[core3d][assembly][axistangent]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(2.0);
    const int idxB = asm_.addComponent(b);
    // B starts off on the +X side of A's axis, tilted, at some arbitrary Z along the shared axis.
    asm_.components()[static_cast<std::size_t>(idxB)].placement.SetTranslationPart(gp_Vec(20.0, 0.0, 7.0));

    Mate mate;
    mate.type = MateType::AxisTangentExternal;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0; // A's axis: the world Z axis through the origin
    mate.bdx = 1.0; mate.bdy = 0.0; mate.bdz = 1.0; // B's own axis starts tilted 45 degrees off Z
    mate.value = 2.0;  // radiusA
    mate.value2 = 3.0; // radiusB
    asm_.addMate(mate);

    asm_.solve();

    const gp_Trsf& placement = asm_.components()[static_cast<std::size_t>(idxB)].placement;
    const gp_Pnt axisPoint = gp_Pnt(0, 0, 0).Transformed(placement);
    const gp_Pnt axisTip = gp_Pnt(1, 0, 1).Transformed(placement);
    const gp_Vec axisDir = gp_Vec(axisPoint, axisTip) / axisPoint.Distance(axisTip);

    // B's axis is now parallel to A's (world Z)...
    REQUIRE(std::abs(axisDir.X()) < 1e-6);
    REQUIRE(std::abs(axisDir.Y()) < 1e-6);
    // ...offset exactly radiusA+radiusB = 5 from A's axis (the world Z axis)...
    REQUIRE(std::hypot(axisPoint.X(), axisPoint.Y()) == Approx(5.0).margin(1e-6));
    // ...staying on the +X side it started on (closest-to-current radial direction)...
    REQUIRE(axisPoint.X() > 0.0);
    // ...and its position ALONG the shared axis (Z) is left untouched.
    REQUIRE(axisPoint.Z() == Approx(7.0).margin(1e-6));
}

TEST_CASE("Assembly AxisTangentInternal mate places two parallel axes |radiusA-radiusB| apart",
          "[core3d][assembly][axistangent]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(2.0);
    const int idxB = asm_.addComponent(b);
    asm_.components()[static_cast<std::size_t>(idxB)].placement.SetTranslationPart(gp_Vec(1.0, 0.0, 0.0));

    Mate mate;
    mate.type = MateType::AxisTangentInternal;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.adz = 1.0;
    mate.bdz = 1.0; // already parallel -- exercises the "axes coincide/aligned" path, not the rotation path
    mate.value = 5.0;  // radiusA (the bore)
    mate.value2 = 2.0; // radiusB (the shaft)
    asm_.addMate(mate);

    asm_.solve();

    const gp_Pnt axisPoint = gp_Pnt(0, 0, 0).Transformed(asm_.components()[static_cast<std::size_t>(idxB)].placement);
    REQUIRE(std::hypot(axisPoint.X(), axisPoint.Y()) == Approx(3.0).margin(1e-6)); // |5 - 2|
}

TEST_CASE("Assembly AxisTangentExternal mate is idempotent: solving twice doesn't drift the offset",
          "[core3d][assembly][axistangent]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);
    AssemblyComponent b;
    b.shape = makeBox(2.0);
    const int idxB = asm_.addComponent(b);

    Mate mate;
    mate.type = MateType::AxisTangentExternal;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.adz = 1.0;
    mate.bdx = 1.0;
    mate.value = 1.5;
    mate.value2 = 1.5;
    asm_.addMate(mate);

    asm_.solve();
    const gp_Pnt first = gp_Pnt(0, 0, 0).Transformed(asm_.components()[static_cast<std::size_t>(idxB)].placement);
    asm_.solve();
    const gp_Pnt second = gp_Pnt(0, 0, 0).Transformed(asm_.components()[static_cast<std::size_t>(idxB)].placement);
    REQUIRE(std::hypot(first.X(), first.Y()) == Approx(3.0).margin(1e-6));
    REQUIRE(std::hypot(second.X(), second.Y()) == Approx(3.0).margin(1e-6));
    REQUIRE(first.Z() == Approx(second.Z()).margin(1e-6));
}

TEST_CASE("Assembly Fixed mate is Concentric's point+direction alignment plus a pinning roll",
          "[core3d][assembly][fixed]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(4.0);
    const int idxB = asm_.addComponent(b);

    Mate mate;
    mate.type = MateType::Fixed;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.ax = 5.0; mate.ay = 5.0; mate.az = 0.0;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0; // A's axis points +Z
    mate.bx = 2.0; mate.by = 2.0; mate.bz = 0.0;
    mate.bdx = 0.0; mate.bdy = 0.0; mate.bdz = 1.0; // B's axis, also local +Z
    mate.value = 90.0; // the roll that pins the last (spin-around-axis) DOF
    asm_.addMate(mate);

    asm_.solve();

    const gp_Trsf& placement = asm_.components()[static_cast<std::size_t>(idxB)].placement;
    const gp_Pnt worldRefB = gp_Pnt(2.0, 2.0, 0.0).Transformed(placement);
    REQUIRE(worldRefB.X() == Approx(5.0).margin(1e-6)); // point coincidence, same as Concentric
    REQUIRE(worldRefB.Y() == Approx(5.0).margin(1e-6));
    REQUIRE(worldRefB.Z() == Approx(0.0).margin(1e-6));

    const gp_Pnt zTipWorld = gp_Pnt(2.0, 2.0, 1.0).Transformed(placement);
    REQUIRE(zTipWorld.Z() == Approx(1.0).margin(1e-6)); // same-sense, not flipped, same as Concentric

    // The roll: a point one unit along B's local +X from its reference
    // point must land one unit along world +Y from B's now-placed
    // reference point -- a 90-degree spin around +Z, exactly Angle's own
    // convention, proving Fixed doesn't leave that DOF free the way
    // Concentric alone does.
    const gp_Pnt xTipWorld = gp_Pnt(3.0, 2.0, 0.0).Transformed(placement);
    REQUIRE((xTipWorld.X() - worldRefB.X()) == Approx(0.0).margin(1e-6));
    REQUIRE((xTipWorld.Y() - worldRefB.Y()) == Approx(1.0).margin(1e-6));
}

TEST_CASE("Assembly Slider mate translates componentB along the shared axis without touching its rotation",
          "[core3d][assembly][slider]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0);
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(4.0);
    const int idxB = asm_.addComponent(b);

    Mate mate;
    mate.type = MateType::Slider;
    mate.componentA = idxA;
    mate.componentB = idxB;
    mate.ax = 5.0; mate.ay = 5.0; mate.az = 0.0;
    mate.adx = 0.0; mate.ady = 0.0; mate.adz = 1.0; // slide axis is world Z
    mate.bx = 2.0; mate.by = 3.0; mate.bz = 1.0;    // B's own reference point, an arbitrary local offset
    mate.value = 7.0;
    asm_.addMate(mate);

    asm_.solve();

    const gp_Trsf& placement = asm_.components()[static_cast<std::size_t>(idxB)].placement;
    // B starts at an identity placement, so before this mate its reference
    // point's world position is exactly its own local coords (2,3,1).
    // Slider only ever adjusts the component ALONG worldDirA (world Z);
    // its off-axis (X/Y) position is left completely alone, unlike every
    // point-mate type, which would snap it onto A's own (5,5) position.
    const gp_Pnt worldRefB = gp_Pnt(2.0, 3.0, 1.0).Transformed(placement);
    REQUIRE(worldRefB.X() == Approx(2.0).margin(1e-6)); // off-axis, untouched
    REQUIRE(worldRefB.Y() == Approx(3.0).margin(1e-6)); // off-axis, untouched
    REQUIRE(worldRefB.Z() == Approx(7.0).margin(1e-6)); // along-axis, pinned to `value`

    // Rotation must be completely untouched (identity) -- a point one unit
    // along B's local +X from its reference point must still be exactly
    // one unit along world +X from it, not rotated at all.
    const gp_Pnt xTipWorld = gp_Pnt(3.0, 3.0, 1.0).Transformed(placement);
    REQUIRE((xTipWorld.X() - worldRefB.X()) == Approx(1.0).margin(1e-6));
    REQUIRE((xTipWorld.Y() - worldRefB.Y()) == Approx(0.0).margin(1e-6));
    REQUIRE((xTipWorld.Z() - worldRefB.Z()) == Approx(0.0).margin(1e-6));
}

TEST_CASE("detectInterferences reports overlapping placed components and skips non-overlapping ones",
         "[core3d][assembly][interference]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0); // [0,10]^3
    a.fixed = true;
    const int idxA = asm_.addComponent(a);

    AssemblyComponent overlapping;
    overlapping.shape = makeBox(4.0); // [0,4]^3 locally
    overlapping.placement.SetTranslationPart(gp_Vec(5.0, 5.0, 5.0)); // world [5,9]^3 -- overlaps A's [0,10]^3
    const int idxOverlap = asm_.addComponent(overlapping);

    AssemblyComponent disjoint;
    disjoint.shape = makeBox(4.0);
    disjoint.placement.SetTranslationPart(gp_Vec(100.0, 100.0, 100.0)); // far away, no overlap
    const int idxDisjoint = asm_.addComponent(disjoint);

    const std::vector<InterferencePair> pairs = detectInterferences(asm_);
    REQUIRE(pairs.size() == 1);
    REQUIRE(((pairs[0].componentA == idxA && pairs[0].componentB == idxOverlap) ||
            (pairs[0].componentA == idxOverlap && pairs[0].componentB == idxA)));
    // The overlap region is [5,9]x[5,9]x[5,9] -- since A spans [0,10] and
    // the box at (5,5,5) spans [5,9] on every axis, the intersection is
    // the whole smaller box: 4*4*4 = 64.
    REQUIRE(pairs[0].interferenceVolume == Approx(64.0).epsilon(0.01));
    (void)idxDisjoint;
}

TEST_CASE("assemblyPlacedShapes transforms each component's own shape by its current world placement, "
         "leaving a null component shape null",
         "[core3d][assembly]") {
    Assembly asm_;
    AssemblyComponent a;
    a.shape = makeBox(10.0); // [0,10]^3 locally, identity placement -- stays there
    a.fixed = true;
    asm_.addComponent(a);

    AssemblyComponent b;
    b.shape = makeBox(4.0); // [0,4]^3 locally
    b.placement.SetTranslationPart(gp_Vec(5.0, 5.0, 5.0));
    asm_.addComponent(b);

    AssemblyComponent nullComp;
    asm_.addComponent(nullComp); // shape deliberately left null

    const std::vector<TopoDS_Shape> placed = assemblyPlacedShapes(asm_);
    REQUIRE(placed.size() == 3);
    REQUIRE_FALSE(placed[0].IsNull());
    REQUIRE_FALSE(placed[1].IsNull());
    REQUIRE(placed[2].IsNull());

    Bnd_Box boxA;
    BRepBndLib::Add(placed[0], boxA);
    double xminA, yminA, zminA, xmaxA, ymaxA, zmaxA;
    boxA.Get(xminA, yminA, zminA, xmaxA, ymaxA, zmaxA);
    REQUIRE(xminA == Approx(0.0).margin(1e-6));
    REQUIRE(xmaxA == Approx(10.0).margin(1e-6));

    Bnd_Box boxB;
    BRepBndLib::Add(placed[1], boxB);
    double xminB, yminB, zminB, xmaxB, ymaxB, zmaxB;
    boxB.Get(xminB, yminB, zminB, xmaxB, ymaxB, zmaxB);
    // Local [0,4]^3 translated to world (5,5,5) becomes world [5,9]^3.
    REQUIRE(xminB == Approx(5.0).margin(1e-6));
    REQUIRE(xmaxB == Approx(9.0).margin(1e-6));
}

TEST_CASE("buildPartsList groups components by name and counts duplicates, in first-appearance order",
         "[core3d][assembly][partslist]") {
    Assembly asm_;
    AssemblyComponent bolt1;
    bolt1.name = "Bolt";
    asm_.addComponent(bolt1);
    AssemblyComponent nut;
    nut.name = "Nut";
    asm_.addComponent(nut);
    AssemblyComponent bolt2;
    bolt2.name = "Bolt";
    asm_.addComponent(bolt2);
    AssemblyComponent bolt3;
    bolt3.name = "Bolt";
    asm_.addComponent(bolt3);

    const std::vector<PartsListEntry> entries = buildPartsList(asm_);
    REQUIRE(entries.size() == 2);
    // Bolt appeared first (component 0), so it leads the list even
    // though Nut only has one instance -- a real BOM's own encounter-
    // order convention, not an alphabetical resort.
    REQUIRE(entries[0].name == "Bolt");
    REQUIRE(entries[0].quantity == 3);
    REQUIRE(entries[1].name == "Nut");
    REQUIRE(entries[1].quantity == 1);
}

TEST_CASE("buildPartsList groups every empty-named component together and returns empty for an empty assembly",
         "[core3d][assembly][partslist]") {
    Assembly asm_;
    REQUIRE(buildPartsList(asm_).empty());

    AssemblyComponent a;
    asm_.addComponent(a); // name left empty
    AssemblyComponent b;
    asm_.addComponent(b); // name left empty
    AssemblyComponent named;
    named.name = "Bracket";
    asm_.addComponent(named);

    const std::vector<PartsListEntry> entries = buildPartsList(asm_);
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].name.empty());
    REQUIRE(entries[0].quantity == 2);
    REQUIRE(entries[1].name == "Bracket");
    REQUIRE(entries[1].quantity == 1);
}

TEST_CASE("patternComponent linear-patterns a component, composing each copy's placement onto the source's own",
         "[core3d][assembly][pattern]") {
    Assembly asm_;
    AssemblyComponent bolt;
    bolt.name = "Bolt";
    bolt.shape = makeBox(1.0);
    // The source is already placed away from the origin -- proves the
    // pattern step composes onto the source's own CURRENT placement,
    // not the component's raw local shape.
    bolt.placement.SetTranslationPart(gp_Vec(100.0, 0.0, 0.0));
    const int sourceIdx = asm_.addComponent(bolt);

    ComponentPatternParams params;
    params.kind = ComponentPatternKind::Linear;
    params.count = 4; // source + 3 copies
    params.dirX = 1.0;
    params.dirY = 0.0;
    params.dirZ = 0.0;
    params.spacing = 10.0;
    const std::vector<int> added = patternComponent(asm_, sourceIdx, params);

    REQUIRE(added.size() == 3);
    REQUIRE(asm_.components().size() == 4);
    for (std::size_t i = 0; i < added.size(); ++i) {
        const AssemblyComponent& copy = asm_.components()[static_cast<std::size_t>(added[i])];
        REQUIRE(copy.fixed);
        REQUIRE(copy.name == "Bolt (" + std::to_string(i + 2) + ")");
        const gp_Pnt worldOrigin = gp_Pnt(0, 0, 0).Transformed(copy.placement);
        // Copy i (1-based added index) sits spacing*(i+1) further along X
        // than the source's own 100.0 -- i.e. 110, 120, 130.
        REQUIRE(worldOrigin.X() == Approx(100.0 + 10.0 * static_cast<double>(i + 1)).margin(1e-6));
        REQUIRE(worldOrigin.Y() == Approx(0.0).margin(1e-6));
    }
}

TEST_CASE("patternComponent polar-patterns a component evenly around an axis", "[core3d][assembly][pattern]") {
    Assembly asm_;
    AssemblyComponent bolt;
    bolt.name = "Bolt";
    bolt.shape = makeBox(1.0);
    bolt.placement.SetTranslationPart(gp_Vec(10.0, 0.0, 0.0)); // 10 units from the Z axis
    const int sourceIdx = asm_.addComponent(bolt);

    ComponentPatternParams params;
    params.kind = ComponentPatternKind::Polar;
    params.count = 4; // source at 0 deg + 3 copies at 90/180/270
    params.axisX = 0.0;
    params.axisY = 0.0;
    params.axisZ = 1.0;
    params.originX = 0.0;
    params.originY = 0.0;
    params.originZ = 0.0;
    params.totalAngleDegrees = 360.0;
    const std::vector<int> added = patternComponent(asm_, sourceIdx, params);

    REQUIRE(added.size() == 3);
    // totalAngle/count (4 instances over 360 degrees = 90 degrees apart)
    // -- deliberately NOT Document3D's own PolarPattern convention of
    // totalAngle/(count-1), which would put the 4th copy exactly back on
    // top of the source for a full 360-degree pattern (see
    // ComponentPatternParams' own comment on why).
    const double expectedAngles[3] = {90.0, 180.0, 270.0};
    for (std::size_t i = 0; i < added.size(); ++i) {
        const AssemblyComponent& copy = asm_.components()[static_cast<std::size_t>(added[i])];
        const gp_Pnt worldOrigin = gp_Pnt(0, 0, 0).Transformed(copy.placement);
        const double angleRad = expectedAngles[i] * M_PI / 180.0;
        REQUIRE(worldOrigin.X() == Approx(10.0 * std::cos(angleRad)).margin(1e-6));
        REQUIRE(worldOrigin.Y() == Approx(10.0 * std::sin(angleRad)).margin(1e-6));
    }
}

TEST_CASE("patternComponent rejects an out-of-range source, count < 2, or a degenerate direction/axis",
         "[core3d][assembly][pattern]") {
    Assembly asm_;
    AssemblyComponent bolt;
    bolt.shape = makeBox(1.0);
    asm_.addComponent(bolt);

    ComponentPatternParams params;
    params.count = 3;
    params.dirX = 1.0;

    REQUIRE(patternComponent(asm_, -1, params).empty());
    REQUIRE(patternComponent(asm_, 5, params).empty());

    ComponentPatternParams tooFew;
    tooFew.count = 1;
    REQUIRE(patternComponent(asm_, 0, tooFew).empty());

    ComponentPatternParams zeroDir;
    zeroDir.count = 3;
    zeroDir.dirX = zeroDir.dirY = zeroDir.dirZ = 0.0;
    REQUIRE(patternComponent(asm_, 0, zeroDir).empty());

    ComponentPatternParams zeroAxis;
    zeroAxis.kind = ComponentPatternKind::Polar;
    zeroAxis.count = 3;
    zeroAxis.axisX = zeroAxis.axisY = zeroAxis.axisZ = 0.0;
    REQUIRE(patternComponent(asm_, 0, zeroAxis).empty());

    // None of the rejected calls actually added anything.
    REQUIRE(asm_.components().size() == 1);
}
