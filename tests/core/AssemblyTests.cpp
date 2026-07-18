#include "core/core3d/Assembly.h"

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
