#include "core/core3d/Document3D.h"
#include "core/core3d/Pick3D.h"
#include "core/core3d/TopoNaming.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace lcad;
using Catch::Approx;

namespace {

double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

Sketch makeRectangleSketch(double w, double h) {
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(0, 0), true);
    const int p1 = sketch.addPoint(Point2D(w, 0));
    const int p2 = sketch.addPoint(Point2D(w, h));
    const int p3 = sketch.addPoint(Point2D(0, h));
    sketch.addLine(p0, p1);
    sketch.addLine(p1, p2);
    sketch.addLine(p2, p3);
    sketch.addLine(p3, p0);
    return sketch;
}

} // namespace

TEST_CASE("Document3D Pad extrudes a sketch profile into a solid with the correct volume", "[core3d][pad]") {
    Document3D doc;
    const int sketchIdx = doc.addSketch(makeRectangleSketch(10.0, 5.0));

    Feature3D pad;
    pad.type = FeatureType::Pad;
    pad.sketchIndex = sketchIdx;
    pad.p1 = 4.0; // height
    const int padIdx = doc.addFeature(pad);

    REQUIRE(doc.isValid(padIdx));
    REQUIRE(volumeOf(doc.shapeAt(padIdx)) == Approx(10.0 * 5.0 * 4.0).margin(1e-6));
}

TEST_CASE("Document3D Pad in cutMode pockets a hole out of an existing solid", "[core3d][pad][pocket]") {
    Document3D doc;

    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    const int sketchIdx = doc.addSketch(makeRectangleSketch(20.0, 20.0));
    Feature3D pocket;
    pocket.type = FeatureType::Pad;
    pocket.sketchIndex = sketchIdx;
    pocket.p1 = 5.0;
    pocket.inputA = boxIdx;
    pocket.cutMode = true;
    pocket.posZ = 20.0; // start the pocket cut from the box's top face (Z=20) downward
    pocket.dirX = 0;
    pocket.dirY = 0;
    pocket.dirZ = -1;
    const int pocketIdx = doc.addFeature(pocket);

    REQUIRE(doc.isValid(pocketIdx));
    const double boxVolume = 20.0 * 20.0 * 20.0;
    const double pocketVolume = 20.0 * 20.0 * 5.0;
    REQUIRE(volumeOf(doc.shapeAt(pocketIdx)) == Approx(boxVolume - pocketVolume).margin(1e-3));
}

TEST_CASE("Document3D Revolve sweeps a sketch profile around an axis", "[core3d][revolve]") {
    Document3D doc;
    // A rectangle offset from the Y axis, revolved 360 degrees around Y,
    // makes a hollow cylinder (a washer/tube) -- classic revolve test case.
    Sketch sketch;
    const int p0 = sketch.addPoint(Point2D(5, 0), true);
    const int p1 = sketch.addPoint(Point2D(8, 0));
    const int p2 = sketch.addPoint(Point2D(8, 10));
    const int p3 = sketch.addPoint(Point2D(5, 10));
    sketch.addLine(p0, p1);
    sketch.addLine(p1, p2);
    sketch.addLine(p2, p3);
    sketch.addLine(p3, p0);
    const int sketchIdx = doc.addSketch(sketch);

    Feature3D revolve;
    revolve.type = FeatureType::Revolve;
    revolve.sketchIndex = sketchIdx;
    revolve.p1 = 360.0;
    revolve.posX = 0;
    revolve.posY = 0;
    revolve.posZ = 0;
    revolve.dirX = 0;
    revolve.dirY = 1; // axis along Y
    revolve.dirZ = 0;
    const int revolveIdx = doc.addFeature(revolve);

    REQUIRE(doc.isValid(revolveIdx));
    // Pappus's centroid theorem: V = 2*pi*R_centroid*Area, R_centroid = 6.5, Area = 3*10 = 30.
    const double expected = 2.0 * M_PI * 6.5 * 30.0;
    REQUIRE(volumeOf(doc.shapeAt(revolveIdx)) == Approx(expected).margin(1.0));
}

TEST_CASE("Document3D Fillet rounds every edge of a box, changing its volume slightly", "[core3d][fillet]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    Feature3D fillet;
    fillet.type = FeatureType::Fillet;
    fillet.inputA = boxIdx;
    fillet.p1 = 2.0;
    const int filletIdx = doc.addFeature(fillet);

    REQUIRE(doc.isValid(filletIdx));
    const double boxVolume = 20.0 * 20.0 * 20.0;
    // Rounding every edge removes material, so the filleted box must be
    // smaller than the sharp one, but not by a huge amount for a small radius.
    const double filletVolume = volumeOf(doc.shapeAt(filletIdx));
    REQUIRE(filletVolume < boxVolume);
    REQUIRE(filletVolume > boxVolume * 0.9);
}

TEST_CASE("Document3D Chamfer bevels every edge of a box, changing its volume slightly", "[core3d][chamfer]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    Feature3D chamfer;
    chamfer.type = FeatureType::Chamfer;
    chamfer.inputA = boxIdx;
    chamfer.p1 = 2.0;
    const int chamferIdx = doc.addFeature(chamfer);

    REQUIRE(doc.isValid(chamferIdx));
    const double boxVolume = 20.0 * 20.0 * 20.0;
    const double chamferVolume = volumeOf(doc.shapeAt(chamferIdx));
    REQUIRE(chamferVolume < boxVolume);
    REQUIRE(chamferVolume > boxVolume * 0.9);
}

TEST_CASE("Document3D Fillet with specific edgeIndices rounds less material than every-edge mode",
          "[core3d][fillet][pick]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);
    const double boxVolume = 20.0 * 20.0 * 20.0;

    // Find a real edge via Pick3D (the same mechanism a viewport click
    // would use), rather than assuming which OCCT edge index is which.
    PickRay ray;
    ray.origin = {20.1, 20.0, -50.0};
    ray.direction = {0.0, 0.0, 1.0};
    const auto picked = pickEdge(doc.shapeAt(boxIdx), ray, 0.5);
    REQUIRE(picked.has_value());

    Feature3D oneEdgeFillet;
    oneEdgeFillet.type = FeatureType::Fillet;
    oneEdgeFillet.inputA = boxIdx;
    oneEdgeFillet.p1 = 2.0;
    oneEdgeFillet.edgeIndices = {picked->edgeIndex};
    const int oneEdgeIdx = doc.addFeature(oneEdgeFillet);
    REQUIRE(doc.isValid(oneEdgeIdx));
    const double oneEdgeVolume = volumeOf(doc.shapeAt(oneEdgeIdx));

    Feature3D everyEdgeFillet;
    everyEdgeFillet.type = FeatureType::Fillet;
    everyEdgeFillet.inputA = boxIdx;
    everyEdgeFillet.p1 = 2.0;
    const int everyEdgeIdx = doc.addFeature(everyEdgeFillet);
    REQUIRE(doc.isValid(everyEdgeIdx));
    const double everyEdgeVolume = volumeOf(doc.shapeAt(everyEdgeIdx));

    // Rounding just one of the box's 12 edges removes strictly less
    // material than rounding all of them, but still strictly less than
    // the sharp box's own volume.
    REQUIRE(oneEdgeVolume < boxVolume);
    REQUIRE(oneEdgeVolume > everyEdgeVolume);
}

TEST_CASE("Document3D Fillet where every edgeIndices entry is out of range is invalid, not a silent no-op",
          "[core3d][fillet][pick]") {
    // A real edge case, not just a hypothetical: BRepFilletAPI_MakeFillet::
    // Build() throws if asked to build with zero edges actually added (the
    // first version of this out-of-range handling assumed it would
    // gracefully no-op instead -- caught by this test throwing, not by
    // review).
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    Feature3D fillet;
    fillet.type = FeatureType::Fillet;
    fillet.inputA = boxIdx;
    fillet.p1 = 2.0;
    fillet.edgeIndices = {999}; // out of range -- no edge actually gets added
    const int filletIdx = doc.addFeature(fillet);

    REQUIRE_FALSE(doc.isValid(filletIdx));
}

TEST_CASE("Document3D Shell hollows a box with one open face to the exact expected volume",
         "[core3d][shell][pick]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    // Pick the box's top face (straight down from above), the same
    // mechanism a viewport click would use.
    PickRay ray;
    ray.origin = {10.0, 10.0, 50.0};
    ray.direction = {0.0, 0.0, -1.0};
    const auto picked = pickFace(doc.shapeAt(boxIdx), ray);
    REQUIRE(picked.has_value());

    Feature3D shell;
    shell.type = FeatureType::Shell;
    shell.inputA = boxIdx;
    shell.p1 = 2.0; // wall thickness
    shell.faceIndices = {picked->faceIndex};
    const int shellIdx = doc.addFeature(shell);
    REQUIRE(doc.isValid(shellIdx));

    // Removing the top face and offsetting the other 5 faces inward by 2
    // leaves a cavity open at the top: x/y each inset by 2 on both sides
    // (16x16), z inset by 2 only at the bottom (open at z=20) -- an exact
    // box-minus-box volume for this perfectly axis-aligned case.
    const double boxVolume = 20.0 * 20.0 * 20.0;
    const double cavityVolume = 16.0 * 16.0 * 18.0;
    REQUIRE(volumeOf(doc.shapeAt(shellIdx)) == Approx(boxVolume - cavityVolume).margin(1.0));
}

TEST_CASE("Document3D Shell with no faceIndices is invalid, not a sealed hollow solid", "[core3d][shell]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    Feature3D shell;
    shell.type = FeatureType::Shell;
    shell.inputA = boxIdx;
    shell.p1 = 2.0;
    // faceIndices left empty -- no face to open the shell through.
    const int shellIdx = doc.addFeature(shell);

    REQUIRE_FALSE(doc.isValid(shellIdx));
}

TEST_CASE("Document3D Loft between two circles builds a frustum with the exact classical volume",
         "[core3d][loft]") {
    Document3D doc;
    Sketch bottom;
    bottom.addCircle(bottom.addPoint(Point2D(0, 0), true), 10.0);
    Sketch top;
    top.addCircle(top.addPoint(Point2D(0, 0), true), 5.0);
    const int bottomIdx = doc.addSketch(bottom);
    const int topIdx = doc.addSketch(top);

    Feature3D loft;
    loft.type = FeatureType::Loft;
    loft.sketchIndices = {bottomIdx, topIdx};
    loft.p1 = 20.0; // total height
    const int loftIdx = doc.addFeature(loft);

    REQUIRE(doc.isValid(loftIdx));
    const double r1 = 10.0, r2 = 5.0, height = 20.0;
    const double expectedVolume = (M_PI * height / 3.0) * (r1 * r1 + r1 * r2 + r2 * r2);
    REQUIRE(volumeOf(doc.shapeAt(loftIdx)) == Approx(expectedVolume).epsilon(0.02));
}

TEST_CASE("Document3D Loft through three profiles builds a single continuous solid", "[core3d][loft]") {
    Document3D doc;
    Sketch bottom;
    bottom.addCircle(bottom.addPoint(Point2D(0, 0), true), 8.0);
    Sketch middle;
    middle.addCircle(middle.addPoint(Point2D(0, 0), true), 10.0);
    Sketch top;
    top.addCircle(top.addPoint(Point2D(0, 0), true), 6.0);
    const int bottomIdx = doc.addSketch(bottom);
    const int middleIdx = doc.addSketch(middle);
    const int topIdx = doc.addSketch(top);

    Feature3D loft;
    loft.type = FeatureType::Loft;
    loft.sketchIndices = {bottomIdx, middleIdx, topIdx};
    loft.p1 = 30.0;
    const int loftIdx = doc.addFeature(loft);

    REQUIRE(doc.isValid(loftIdx));
    // A bulging middle profile means strictly more volume than a plain
    // 2-profile taper between the same end radii would give.
    const double twoProfileApprox = (M_PI * 30.0 / 3.0) * (8.0 * 8.0 + 8.0 * 6.0 + 6.0 * 6.0);
    REQUIRE(volumeOf(doc.shapeAt(loftIdx)) > twoProfileApprox);
}

TEST_CASE("Document3D Loft's cutMode toggles ruled vs. smooth, producing a genuinely different volume "
         "for the same three profiles",
         "[core3d][loft]") {
    Document3D doc;
    Sketch bottom;
    bottom.addCircle(bottom.addPoint(Point2D(0, 0), true), 8.0);
    Sketch middle;
    middle.addCircle(middle.addPoint(Point2D(0, 0), true), 12.0);
    Sketch top;
    top.addCircle(top.addPoint(Point2D(0, 0), true), 6.0);
    const int bottomIdx = doc.addSketch(bottom);
    const int middleIdx = doc.addSketch(middle);
    const int topIdx = doc.addSketch(top);

    Feature3D smoothLoft;
    smoothLoft.type = FeatureType::Loft;
    smoothLoft.sketchIndices = {bottomIdx, middleIdx, topIdx};
    smoothLoft.p1 = 30.0;
    smoothLoft.cutMode = false; // smooth (default) -- a BSpline-blended surface
    const int smoothIdx = doc.addFeature(smoothLoft);
    REQUIRE(doc.isValid(smoothIdx));

    Feature3D ruledLoft = smoothLoft;
    ruledLoft.cutMode = true; // ruled -- straight-line generators between profiles
    const int ruledIdx = doc.addFeature(ruledLoft);
    REQUIRE(doc.isValid(ruledIdx));

    const double smoothVolume = volumeOf(doc.shapeAt(smoothIdx));
    const double ruledVolume = volumeOf(doc.shapeAt(ruledIdx));
    // Genuinely different surfaces (a smooth blend bulges past a ruled
    // surface's straight-line taper through the same bulging middle
    // profile), not just numerical noise between two nominally-identical
    // builds.
    REQUIRE(std::abs(smoothVolume - ruledVolume) > 1.0);
}

TEST_CASE("Document3D Loft rejects fewer than 2 profiles or a non-positive height", "[core3d][loft]") {
    Document3D doc;
    Sketch circle;
    circle.addCircle(circle.addPoint(Point2D(0, 0), true), 5.0);
    const int idx = doc.addSketch(circle);

    Feature3D oneProfile;
    oneProfile.type = FeatureType::Loft;
    oneProfile.sketchIndices = {idx};
    oneProfile.p1 = 10.0;
    REQUIRE_FALSE(doc.isValid(doc.addFeature(oneProfile)));

    Feature3D zeroHeight;
    zeroHeight.type = FeatureType::Loft;
    zeroHeight.sketchIndices = {idx, idx};
    zeroHeight.p1 = 0.0;
    REQUIRE_FALSE(doc.isValid(doc.addFeature(zeroHeight)));
}

TEST_CASE("Document3D Sweep of a circle along a straight path matches a cylinder's exact volume",
         "[core3d][sweep]") {
    Document3D doc;
    Sketch profile;
    profile.addCircle(profile.addPoint(Point2D(0, 0), true), 3.0);
    const int profileIdx = doc.addSketch(profile);

    Sketch path;
    const int p0 = path.addPoint(Point2D(0, 0), true);
    const int p1 = path.addPoint(Point2D(20, 0), true);
    path.addLine(p0, p1);
    const int pathIdx = doc.addSketch(path);

    Feature3D sweep;
    sweep.type = FeatureType::Sweep;
    sweep.sketchIndex = profileIdx;
    sweep.pathSketchIndex = pathIdx;
    const int sweepIdx = doc.addFeature(sweep);

    REQUIRE(doc.isValid(sweepIdx));
    const double radius = 3.0, length = 20.0;
    REQUIRE(volumeOf(doc.shapeAt(sweepIdx)) == Approx(M_PI * radius * radius * length).epsilon(1e-3));
}

TEST_CASE("Document3D Sweep follows a multi-segment (sharp-cornered) path, with roughly the combined "
         "volume of both legs",
         "[core3d][sweep]") {
    // A real, disclosed capability now (see FeatureType::Sweep's own
    // comment): BRepOffsetAPI_MakePipeShell with an explicit RightCorner
    // transition mode handles a sharp-cornered polyline spine that
    // MakePipe's own G1-continuity requirement couldn't.
    Document3D doc;
    Sketch profile;
    profile.addCircle(profile.addPoint(Point2D(0, 0), true), 2.0);
    const int profileIdx = doc.addSketch(profile);

    Sketch path;
    const int p0 = path.addPoint(Point2D(0, 0), true);
    const int p1 = path.addPoint(Point2D(10, 0), true);
    const int p2 = path.addPoint(Point2D(10, 10), true);
    path.addLine(p0, p1);
    path.addLine(p1, p2);
    const int pathIdx = doc.addSketch(path);

    Feature3D sweep;
    sweep.type = FeatureType::Sweep;
    sweep.sketchIndex = profileIdx;
    sweep.pathSketchIndex = pathIdx;
    const int sweepIdx = doc.addFeature(sweep);

    REQUIRE(doc.isValid(sweepIdx));
    // Two 10-unit legs of a radius-2 pipe meeting at a right angle: a
    // RightCorner miter joint between two EQUAL-radius round pipes is a
    // symmetric wedge exchange (the sliver added to one leg's own miter
    // cut is congruent to the sliver it takes from the other), so the
    // total volume comes out essentially identical to two independent
    // cylinders' worth, not more.
    const double radius = 2.0;
    const double twoCylinders = M_PI * radius * radius * 20.0;
    REQUIRE(volumeOf(doc.shapeAt(sweepIdx)) == Approx(twoCylinders).epsilon(0.01));
}

TEST_CASE("Document3D Sweep follows a filleted-corner path (line, tangent arc, line), matching the sum "
         "of a cylinder, a quarter-torus, and a cylinder",
         "[core3d][sweep]") {
    Document3D doc;
    Sketch profile;
    profile.addCircle(profile.addPoint(Point2D(0, 0), true), 1.0);
    const int profileIdx = doc.addSketch(profile);

    // A straight run along +X, a tangent quarter-circle arc (radius 10)
    // curving up to +Y, then a straight run along +Y -- a genuinely
    // G1-continuous "filleted corner" path, real FreeCAD's own most
    // common Sweep use beyond a single straight line.
    Sketch path;
    const int p0 = path.addPoint(Point2D(0, 0), true);
    const int p1 = path.addPoint(Point2D(10, 0), true);
    const int arcCenter = path.addPoint(Point2D(10, 10), true);
    const int p2 = path.addPoint(Point2D(20, 10), true);
    const int p3 = path.addPoint(Point2D(20, 20), true);
    path.addLine(p0, p1);
    path.addArc(arcCenter, p1, p2, 10.0, true);
    path.addLine(p2, p3);
    const int pathIdx = doc.addSketch(path);

    Feature3D sweep;
    sweep.type = FeatureType::Sweep;
    sweep.sketchIndex = profileIdx;
    sweep.pathSketchIndex = pathIdx;
    const int sweepIdx = doc.addFeature(sweep);

    REQUIRE(doc.isValid(sweepIdx));
    // Cylinder + quarter-torus + cylinder, all radius-1 profile: since
    // every join here is genuinely tangent (no real corner for
    // RightCorner's own miter to do anything at), this should match the
    // plain Pappus sum closely.
    const double profileArea = M_PI * 1.0 * 1.0;
    const double archLength = (M_PI / 2.0) * 10.0; // quarter circle, radius 10
    const double expected = profileArea * 10.0 + profileArea * archLength + profileArea * 10.0;
    REQUIRE(volumeOf(doc.shapeAt(sweepIdx)) == Approx(expected).epsilon(0.02));
}

TEST_CASE("Document3D Sweep rejects an empty path or out-of-range sketch indices", "[core3d][sweep]") {
    Document3D doc;
    Sketch profile;
    profile.addCircle(profile.addPoint(Point2D(0, 0), true), 2.0);
    const int profileIdx = doc.addSketch(profile);

    Sketch emptyPath;
    const int emptyPathIdx = doc.addSketch(emptyPath);

    Feature3D noPathLines;
    noPathLines.type = FeatureType::Sweep;
    noPathLines.sketchIndex = profileIdx;
    noPathLines.pathSketchIndex = emptyPathIdx;
    REQUIRE_FALSE(doc.isValid(doc.addFeature(noPathLines)));

    Feature3D badIndex;
    badIndex.type = FeatureType::Sweep;
    badIndex.sketchIndex = profileIdx;
    badIndex.pathSketchIndex = 999;
    REQUIRE_FALSE(doc.isValid(doc.addFeature(badIndex)));
}

TEST_CASE("Document3D Draft tapers a box's side face to the exact expected trapezoidal-prism volume",
         "[core3d][draft][pick]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    // Find the box's own +X face via Pick3D, the same mechanism a
    // viewport click would use.
    PickRay ray;
    ray.origin = {50.0, 10.0, 10.0};
    ray.direction = {-1.0, 0.0, 0.0};
    const auto picked = pickFace(doc.shapeAt(boxIdx), ray);
    REQUIRE(picked.has_value());

    Feature3D draft;
    draft.type = FeatureType::Draft;
    draft.inputA = boxIdx;
    draft.faceIndices = {picked->faceIndex};
    draft.p1 = 10.0; // degrees
    draft.dirX = 0.0;
    draft.dirY = 0.0;
    draft.dirZ = 1.0; // pull direction == neutral plane normal, +Z
    draft.posX = draft.posY = draft.posZ = 0.0; // neutral plane through the bottom face, z=0
    const int draftIdx = doc.addFeature(draft);

    REQUIRE(doc.isValid(draftIdx));

    // The +X face pivots about its own unchanged bottom edge (on the
    // neutral plane, z=0), tapering inward as Z increases -- the
    // resulting solid is a trapezoidal prism: at height z, the x-extent
    // is (20 - z*tan(angle)), constant across the full 20-unit Y depth.
    const double angleRad = 10.0 * M_PI / 180.0;
    const double expectedVolume = 20.0 * (20.0 * 20.0 - std::tan(angleRad) * (20.0 * 20.0 / 2.0));
    REQUIRE(volumeOf(doc.shapeAt(draftIdx)) == Approx(expectedVolume).epsilon(1e-3));
}

TEST_CASE("Document3D Draft rejects an empty faceIndices or a zero-length pull direction", "[core3d][draft]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    Feature3D noFaces;
    noFaces.type = FeatureType::Draft;
    noFaces.inputA = boxIdx;
    noFaces.p1 = 5.0;
    noFaces.dirZ = 1.0;
    REQUIRE_FALSE(doc.isValid(doc.addFeature(noFaces)));

    Feature3D zeroDir;
    zeroDir.type = FeatureType::Draft;
    zeroDir.inputA = boxIdx;
    zeroDir.faceIndices = {0};
    zeroDir.p1 = 5.0;
    zeroDir.dirX = zeroDir.dirY = zeroDir.dirZ = 0.0;
    REQUIRE_FALSE(doc.isValid(doc.addFeature(zeroDir)));
}

TEST_CASE("Document3D Draft re-resolves a stale faceIndices entry via its own faceFingerprints",
         "[core3d][draft][toponaming]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    PickRay ray;
    ray.origin = {50.0, 10.0, 10.0};
    ray.direction = {-1.0, 0.0, 0.0};
    const auto picked = pickFace(doc.shapeAt(boxIdx), ray);
    REQUIRE(picked.has_value());
    const auto fingerprint = fingerprintFace(doc.shapeAt(boxIdx), picked->faceIndex);
    REQUIRE(fingerprint.has_value());

    Feature3D correctDraft;
    correctDraft.type = FeatureType::Draft;
    correctDraft.inputA = boxIdx;
    correctDraft.faceIndices = {picked->faceIndex};
    correctDraft.p1 = 10.0;
    correctDraft.dirZ = 1.0;
    const int correctIdx = doc.addFeature(correctDraft);
    REQUIRE(doc.isValid(correctIdx));
    const double correctVolume = volumeOf(doc.shapeAt(correctIdx));

    // A deliberately wrong raw index (some other face of the box's 6),
    // but the right fingerprint -- same stale-index simulation the
    // Fillet/Chamfer/Shell topo-naming tests already use.
    const int wrongIndex = (picked->faceIndex + 3) % 6;
    Feature3D staleDraft;
    staleDraft.type = FeatureType::Draft;
    staleDraft.inputA = boxIdx;
    staleDraft.faceIndices = {wrongIndex};
    staleDraft.faceFingerprints = {*fingerprint};
    staleDraft.p1 = 10.0;
    staleDraft.dirZ = 1.0;
    const int staleIdx = doc.addFeature(staleDraft);
    REQUIRE(doc.isValid(staleIdx));

    REQUIRE(volumeOf(doc.shapeAt(staleIdx)) == Approx(correctVolume).margin(1e-6));
}

TEST_CASE("Document3D LinearPattern fuses count non-overlapping copies", "[core3d][pattern]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 5.0;
    const int boxIdx = doc.addFeature(box);

    Feature3D pattern;
    pattern.type = FeatureType::LinearPattern;
    pattern.inputA = boxIdx;
    pattern.p1 = 10.0; // spacing, bigger than the box so copies don't overlap
    pattern.count = 4;
    pattern.dirX = 1;
    pattern.dirY = 0;
    pattern.dirZ = 0;
    const int patternIdx = doc.addFeature(pattern);

    REQUIRE(doc.isValid(patternIdx));
    REQUIRE(volumeOf(doc.shapeAt(patternIdx)) == Approx(4 * 5.0 * 5.0 * 5.0).margin(1e-3));
}

TEST_CASE("Document3D ScaledPattern fuses the original with a uniformly-scaled, non-overlapping copy",
          "[core3d][pattern][scaledpattern]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 5.0; // occupies [0,5]^3
    const int boxIdx = doc.addFeature(box);

    Feature3D pattern;
    pattern.type = FeatureType::ScaledPattern;
    pattern.inputA = boxIdx;
    pattern.count = 2;                       // original + 1 scaled copy
    pattern.p1 = 2.0;                         // final copy scaled 2x
    pattern.posX = pattern.posY = pattern.posZ = 100.0; // scaling center far from the box: no overlap
    const int patternIdx = doc.addFeature(pattern);

    REQUIRE(doc.isValid(patternIdx));
    // Original: 5^3 = 125. Scaled copy: side doubles to 10, volume 10^3 = 1000.
    // Scaling about a distant center moves the copy far from the original
    // (see this test's own math in the review comment), so the fused
    // volume is exactly additive, not partially overlapping.
    REQUIRE(volumeOf(doc.shapeAt(patternIdx)) == Approx(125.0 + 1000.0).margin(1e-3));
}

TEST_CASE("Document3D ScaledPattern with count 1 leaves the source shape unchanged", "[core3d][pattern][scaledpattern]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 5.0;
    const int boxIdx = doc.addFeature(box);

    Feature3D pattern;
    pattern.type = FeatureType::ScaledPattern;
    pattern.inputA = boxIdx;
    pattern.count = 1; // no extra copies
    pattern.p1 = 3.0;  // would matter if there were a second copy, but there isn't
    const int patternIdx = doc.addFeature(pattern);

    REQUIRE(doc.isValid(patternIdx));
    REQUIRE(volumeOf(doc.shapeAt(patternIdx)) == Approx(125.0).margin(1e-3));
}

TEST_CASE("Document3D ScaledPattern rejects a missing target or a zero count", "[core3d][pattern][scaledpattern]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 5.0;
    const int boxIdx = doc.addFeature(box);

    Feature3D badTarget;
    badTarget.type = FeatureType::ScaledPattern;
    badTarget.inputA = -1;
    badTarget.count = 2;
    REQUIRE_FALSE(doc.isValid(doc.addFeature(badTarget)));

    Feature3D zeroCount;
    zeroCount.type = FeatureType::ScaledPattern;
    zeroCount.inputA = boxIdx;
    zeroCount.count = 0;
    REQUIRE_FALSE(doc.isValid(doc.addFeature(zeroCount)));
}

TEST_CASE("Document3D Mirror fuses a solid with its reflection across a plane", "[core3d][mirror]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 5.0;
    box.posX = 10.0; // offset from the mirror plane so the reflection doesn't overlap the original
    const int boxIdx = doc.addFeature(box);

    Feature3D mirror;
    mirror.type = FeatureType::Mirror;
    mirror.inputA = boxIdx;
    mirror.posX = 0.0; // mirror plane through the origin
    mirror.dirX = 1.0; // normal along X (the YZ plane)
    mirror.dirY = 0.0;
    mirror.dirZ = 0.0;
    const int mirrorIdx = doc.addFeature(mirror);

    REQUIRE(doc.isValid(mirrorIdx));
    REQUIRE(volumeOf(doc.shapeAt(mirrorIdx)) == Approx(2 * 5.0 * 5.0 * 5.0).margin(1e-3));
}

TEST_CASE("Document3D Pad on a non-default sketch plane produces correctly oriented geometry", "[core3d][pad][plane]") {
    // A rectangle sketched on the XZ plane, padded along that plane's own
    // normal (world -Y): the resulting solid must span X and Z (the
    // sketch's own footprint) and Y (the pad thickness), NOT span Y and Z
    // like a flat-XY-plane pad would if the placement were silently
    // ignored.
    Document3D doc;
    Sketch sketch = makeRectangleSketch(10.0, 5.0); // local (x,y) -> world (x, 0, y) on the XZ plane
    sketch.setPlacement(SketchPlane::XZ());
    const int sketchIdx = doc.addSketch(sketch);

    Feature3D pad;
    pad.type = FeatureType::Pad;
    pad.sketchIndex = sketchIdx;
    pad.p1 = 3.0; // thickness, along the pad direction
    pad.dirX = 0.0;
    pad.dirY = -1.0; // XZ plane's own normal
    pad.dirZ = 0.0;
    const int padIdx = doc.addFeature(pad);

    REQUIRE(doc.isValid(padIdx));
    const TopoDS_Shape& shape = doc.shapeAt(padIdx);
    REQUIRE(volumeOf(shape) == Approx(10.0 * 5.0 * 3.0).margin(1e-6));

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    REQUIRE((xmax - xmin) == Approx(10.0)); // the sketch's own width, along world X
    REQUIRE((ymax - ymin) == Approx(3.0));  // the pad thickness, along world Y (the plane's normal)
    REQUIRE((zmax - zmin) == Approx(5.0));  // the sketch's own height, along world Z
}

TEST_CASE("Document3D Pad on the YZ plane with an offset places geometry correctly", "[core3d][pad][plane]") {
    Document3D doc;
    Sketch sketch = makeRectangleSketch(6.0, 4.0); // local (x,y) -> world (offset, x, y) on the YZ plane
    sketch.setPlacement(SketchPlane::YZ(12.0));    // offset 12 along world X
    const int sketchIdx = doc.addSketch(sketch);

    Feature3D pad;
    pad.type = FeatureType::Pad;
    pad.sketchIndex = sketchIdx;
    pad.p1 = 2.0;
    pad.dirX = 1.0; // YZ plane's own normal
    pad.dirY = 0.0;
    pad.dirZ = 0.0;
    const int padIdx = doc.addFeature(pad);

    REQUIRE(doc.isValid(padIdx));
    const TopoDS_Shape& shape = doc.shapeAt(padIdx);
    REQUIRE(volumeOf(shape) == Approx(6.0 * 4.0 * 2.0).margin(1e-6));

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    REQUIRE(xmin == Approx(12.0)); // starts exactly at the plane's own offset
    REQUIRE((xmax - xmin) == Approx(2.0));
    REQUIRE((ymax - ymin) == Approx(6.0));
    REQUIRE((zmax - zmin) == Approx(4.0));
}

TEST_CASE("Document3D expression-driven parameter reacts to variable changes", "[core3d][expression]") {
    Document3D doc;
    doc.setVariable("Width", 5.0);

    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = 999.0; // deliberately wrong initial value: the expression must override it
    box.p2 = box.p3 = 4.0;
    box.expressions["p1"] = "Width*2";
    const int boxIdx = doc.addFeature(box);

    REQUIRE(doc.isValid(boxIdx));
    REQUIRE(volumeOf(doc.shapeAt(boxIdx)) == Approx(10.0 * 4.0 * 4.0).margin(1e-6));
    REQUIRE(doc.findFeature(boxIdx)->p1 == Approx(10.0)); // the field itself reflects the evaluated result

    // Changing the variable recomputes every feature that depends on it.
    doc.setVariable("Width", 8.0);
    REQUIRE(volumeOf(doc.shapeAt(boxIdx)) == Approx(16.0 * 4.0 * 4.0).margin(1e-6));
    REQUIRE(doc.findFeature(boxIdx)->p1 == Approx(16.0));
}

TEST_CASE("Document3D expression referencing an unknown variable leaves the previous value", "[core3d][expression]") {
    Document3D doc;

    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = 7.0; // no matching variable exists -- this must survive untouched
    box.p2 = box.p3 = 3.0;
    box.expressions["p1"] = "NoSuchVariable";
    const int boxIdx = doc.addFeature(box);

    REQUIRE(doc.isValid(boxIdx));
    REQUIRE(doc.findFeature(boxIdx)->p1 == Approx(7.0));
    REQUIRE(volumeOf(doc.shapeAt(boxIdx)) == Approx(7.0 * 3.0 * 3.0).margin(1e-6));
}

TEST_CASE("Document3D expression on an unknown field name is silently ignored", "[core3d][expression]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 6.0;
    box.expressions["notARealField"] = "1+1";
    const int boxIdx = doc.addFeature(box);

    REQUIRE(doc.isValid(boxIdx));
    REQUIRE(volumeOf(doc.shapeAt(boxIdx)) == Approx(6.0 * 6.0 * 6.0).margin(1e-6));
}

TEST_CASE("Document3D removeVariable recomputes dependents back to their raw stored value", "[core3d][expression]") {
    Document3D doc;
    doc.setVariable("Size", 4.0);

    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 4.0;
    box.expressions["p1"] = "Size*3";
    const int boxIdx = doc.addFeature(box);
    REQUIRE(doc.findFeature(boxIdx)->p1 == Approx(12.0));

    REQUIRE(doc.removeVariable("Size"));
    REQUIRE_FALSE(doc.removeVariable("Size")); // already gone
    // The expression now fails to resolve (unknown variable), so p1 keeps
    // its LAST successfully evaluated value (12.0), not the original 4.0
    // -- matching applyExpressions' own disclosed "leave previous value"
    // contract exactly.
    REQUIRE(doc.findFeature(boxIdx)->p1 == Approx(12.0));
}

TEST_CASE("Document3D Fillet recovers the intended edge via fingerprint even with a stale wrong raw index",
          "[core3d][fillet][toponaming]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    // Pick a real edge, exactly like the existing "specific edgeIndices"
    // test does, and capture its fingerprint from the box's own shape.
    PickRay ray;
    ray.origin = {20.1, 20.0, -50.0};
    ray.direction = {0.0, 0.0, 1.0};
    const auto picked = pickEdge(doc.shapeAt(boxIdx), ray, 0.5);
    REQUIRE(picked.has_value());
    const auto fingerprint = fingerprintEdge(doc.shapeAt(boxIdx), picked->edgeIndex);
    REQUIRE(fingerprint.has_value());

    // A correctly-indexed fillet on the picked edge -- the reference
    // result the fingerprint-driven one below must match.
    Feature3D correctFillet;
    correctFillet.type = FeatureType::Fillet;
    correctFillet.inputA = boxIdx;
    correctFillet.p1 = 2.0;
    correctFillet.edgeIndices = {picked->edgeIndex};
    const int correctIdx = doc.addFeature(correctFillet);
    REQUIRE(doc.isValid(correctIdx));
    const double correctVolume = volumeOf(doc.shapeAt(correctIdx));

    // A DELIBERATELY WRONG raw index (some other edge of the box's 12),
    // but the RIGHT fingerprint -- simulates exactly what a topological-
    // naming failure looks like: the stored index no longer points where
    // it should, but the fingerprint still identifies the real edge.
    int wrongIndex = (picked->edgeIndex + 5) % 12;
    Feature3D staleFillet;
    staleFillet.type = FeatureType::Fillet;
    staleFillet.inputA = boxIdx;
    staleFillet.p1 = 2.0;
    staleFillet.edgeIndices = {wrongIndex};
    staleFillet.edgeFingerprints = {*fingerprint};
    const int staleIdx = doc.addFeature(staleFillet);
    REQUIRE(doc.isValid(staleIdx));

    // Must match the CORRECT fillet's volume, not a wrong-edge one --
    // proving recompute actually used the fingerprint to override the
    // stale index rather than trusting it directly.
    REQUIRE(volumeOf(doc.shapeAt(staleIdx)) == Approx(correctVolume).margin(1e-6));
}

TEST_CASE("Document3D Fillet with no fingerprints falls back to trusting the raw index directly (old-format compat)",
          "[core3d][fillet][toponaming]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);
    const double boxVolume = 20.0 * 20.0 * 20.0;

    Feature3D fillet;
    fillet.type = FeatureType::Fillet;
    fillet.inputA = boxIdx;
    fillet.p1 = 2.0;
    fillet.edgeIndices = {0}; // no edgeFingerprints entry at all
    const int filletIdx = doc.addFeature(fillet);

    REQUIRE(doc.isValid(filletIdx));
    REQUIRE(volumeOf(doc.shapeAt(filletIdx)) < boxVolume); // rounded something, using the raw index as before
}
