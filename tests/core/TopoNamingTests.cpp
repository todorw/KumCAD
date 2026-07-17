#include "core/core3d/TopoNaming.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace lcad;
using Catch::Approx;

TEST_CASE("fingerprintEdge captures a real midpoint and length", "[core3d][toponaming]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 4.0, 6.0).Shape();
    const auto fp = fingerprintEdge(box, 0);
    REQUIRE(fp.has_value());
    REQUIRE(fp->length > 0.0);
    // A box's every edge is one of its dx/dy/dz lengths.
    const bool matchesSomeDimension =
        std::abs(fp->length - 10.0) < 1e-6 || std::abs(fp->length - 4.0) < 1e-6 || std::abs(fp->length - 6.0) < 1e-6;
    REQUIRE(matchesSomeDimension);
}

TEST_CASE("fingerprintEdge/fingerprintFace return nullopt out of range", "[core3d][toponaming]") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    REQUIRE_FALSE(fingerprintEdge(box, -1).has_value());
    REQUIRE_FALSE(fingerprintEdge(box, 999).has_value());
    REQUIRE_FALSE(fingerprintFace(box, -1).has_value());
    REQUIRE_FALSE(fingerprintFace(box, 999).has_value());
}

TEST_CASE("resolveEdgeIndex finds the geometrically corresponding edge after a shape rebuild",
          "[core3d][toponaming]") {
    // A box, and the SAME box translated -- simulates "the shape got
    // rebuilt" without relying on OCCT's edge ordering actually changing
    // (which it usually doesn't for a plain resize, making this the
    // cleanest way to prove the fingerprint match itself is correct: the
    // translated box's edge N is verifiably "the same" edge as the
    // original's edge N by construction, and resolveEdgeIndex must find
    // it purely from geometry).
    const TopoDS_Shape original = BRepPrimAPI_MakeBox(10.0, 4.0, 6.0).Shape();
    gp_Trsf move;
    move.SetTranslation(gp_Vec(100.0, 0.0, 0.0));
    const TopoDS_Shape moved = BRepBuilderAPI_Transform(original, move, true).Shape();

    for (int i = 0; i < 12; ++i) { // a box has 12 edges
        const auto originalFp = fingerprintEdge(original, i);
        REQUIRE(originalFp.has_value());
        // The fingerprint we'd actually resolve against: the ORIGINAL
        // edge's own position, but we're matching it against the MOVED
        // shape after also translating the target point the same way
        // (this simulates "captured fingerprint, shape then moved along
        // with everything else" -- the realistic scenario for a Pad
        // whose upstream position expression changed).
        EdgeFingerprint target = *originalFp;
        target.midX += 100.0;

        const int resolved = resolveEdgeIndex(moved, target);
        REQUIRE(resolved >= 0);
        const auto resolvedFp = fingerprintEdge(moved, resolved);
        REQUIRE(resolvedFp.has_value());
        REQUIRE(resolvedFp->midX == Approx(target.midX).margin(1e-6));
        REQUIRE(resolvedFp->midY == Approx(target.midY).margin(1e-6));
        REQUIRE(resolvedFp->midZ == Approx(target.midZ).margin(1e-6));
        REQUIRE(resolvedFp->length == Approx(target.length).margin(1e-6));
    }
}

TEST_CASE("resolveFaceIndex finds the geometrically corresponding face after a shape rebuild",
          "[core3d][toponaming]") {
    const TopoDS_Shape original = BRepPrimAPI_MakeBox(10.0, 4.0, 6.0).Shape();
    gp_Trsf move;
    move.SetTranslation(gp_Vec(0.0, 0.0, 50.0));
    const TopoDS_Shape moved = BRepBuilderAPI_Transform(original, move, true).Shape();

    for (int i = 0; i < 6; ++i) { // a box has 6 faces
        const auto originalFp = fingerprintFace(original, i);
        REQUIRE(originalFp.has_value());
        FaceFingerprint target = *originalFp;
        target.centroidZ += 50.0;

        const int resolved = resolveFaceIndex(moved, target);
        REQUIRE(resolved >= 0);
        const auto resolvedFp = fingerprintFace(moved, resolved);
        REQUIRE(resolvedFp.has_value());
        REQUIRE(resolvedFp->area == Approx(target.area).margin(1e-6));
    }
}

TEST_CASE("resolveEdgeIndex recovers the correct edge even when the raw stored index is wrong",
          "[core3d][toponaming]") {
    // The scenario this mitigation actually exists for: a stale raw
    // index (index 3, say) no longer points at the edge the user
    // originally selected, but the FINGERPRINT captured at selection
    // time still identifies it correctly by geometry.
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 4.0, 6.0).Shape();
    const int trueIndex = 7;
    const auto trueFingerprint = fingerprintEdge(box, trueIndex);
    REQUIRE(trueFingerprint.has_value());

    const int staleWrongIndex = 2; // simulates a topological-naming mismatch
    REQUIRE(staleWrongIndex != trueIndex);

    const int recovered = resolveEdgeIndex(box, *trueFingerprint);
    REQUIRE(recovered == trueIndex); // NOT staleWrongIndex
}

TEST_CASE("resolveEdgeIndex on an edgeless shape returns -1", "[core3d][toponaming]") {
    TopoDS_Shape empty;
    REQUIRE(resolveEdgeIndex(empty, EdgeFingerprint{}) == -1);
    REQUIRE(resolveFaceIndex(empty, FaceFingerprint{}) == -1);
}
