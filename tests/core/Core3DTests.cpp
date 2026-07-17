#include "core/core3d/Commands3D.h"
#include "core/core3d/Document3D.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
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
} // namespace

TEST_CASE("OpenCASCADE builds a real B-rep box with the correct volume", "[core3d][occt]") {
    // The actual proof this integration works: not just "it links", but a
    // real kernel operation (BRepPrimAPI_MakeBox) producing a shape whose
    // measured volume (via BRepGProp, OCCT's own mass-properties module)
    // matches width*height*depth. No display/viewport is needed for this,
    // unlike the 3D viewport itself, which can't be verified in this
    // environment (no display, no working GL rasterizer under offscreen --
    // see the DrawingView precedent for the same limitation on the 2D side).
    BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
    const TopoDS_Shape box = makeBox.Shape();
    REQUIRE_FALSE(box.IsNull());
    REQUIRE(volumeOf(box) == Approx(10.0 * 20.0 * 30.0).margin(1e-6));
}

TEST_CASE("Document3D computes each primitive type with a sane volume", "[core3d][feature]") {
    Document3D doc;

    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = 10;
    box.p2 = 20;
    box.p3 = 30;
    const int boxIdx = doc.addFeature(box);
    REQUIRE(doc.isValid(boxIdx));
    REQUIRE(volumeOf(doc.shapeAt(boxIdx)) == Approx(6000.0).margin(1e-6));

    Feature3D sphere;
    sphere.type = FeatureType::Sphere;
    sphere.p1 = 10;
    const int sphereIdx = doc.addFeature(sphere);
    REQUIRE(doc.isValid(sphereIdx));
    REQUIRE(volumeOf(doc.shapeAt(sphereIdx)) == Approx(4.0 / 3.0 * M_PI * 1000.0).margin(1.0));

    Feature3D cyl;
    cyl.type = FeatureType::Cylinder;
    cyl.p1 = 5;
    cyl.p2 = 10;
    const int cylIdx = doc.addFeature(cyl);
    REQUIRE(doc.isValid(cylIdx));
    REQUIRE(volumeOf(doc.shapeAt(cylIdx)) == Approx(M_PI * 25.0 * 10.0).margin(1.0));
}

TEST_CASE("Document3D rejects a degenerate primitive without crashing", "[core3d][feature]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = 0; // degenerate
    box.p2 = 10;
    box.p3 = 10;
    const int idx = doc.addFeature(box);
    REQUIRE_FALSE(doc.isValid(idx));
}

TEST_CASE("Document3D cuts a cylinder out of a box via a boolean feature", "[core3d][feature][boolean]") {
    Document3D doc;

    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20;
    const int boxIdx = doc.addFeature(box);

    Feature3D hole;
    hole.type = FeatureType::Cylinder;
    hole.p1 = 3;
    hole.p2 = 22; // taller than the box so it fully penetrates both faces
    hole.posX = 10;
    hole.posY = 10;
    hole.posZ = -1;
    const int holeIdx = doc.addFeature(hole);

    Feature3D cut;
    cut.type = FeatureType::Cut;
    cut.inputA = boxIdx;
    cut.inputB = holeIdx;
    const int cutIdx = doc.addFeature(cut);

    REQUIRE(doc.isValid(cutIdx));
    const double boxVolume = 20.0 * 20.0 * 20.0;
    const double holeVolume = M_PI * 9.0 * 20.0;
    REQUIRE(volumeOf(doc.shapeAt(cutIdx)) == Approx(boxVolume - holeVolume).margin(1.0));
}

TEST_CASE("updateFeature edits a primitive's parameter and recomputes every dependent boolean",
         "[core3d][feature][recompute]") {
    Document3D doc;

    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 10;
    const int boxIdx = doc.addFeature(box);

    Feature3D box2;
    box2.type = FeatureType::Box;
    box2.p1 = box2.p2 = box2.p3 = 5;
    box2.posX = 5;
    const int box2Idx = doc.addFeature(box2);

    Feature3D fuse;
    fuse.type = FeatureType::Union;
    fuse.inputA = boxIdx;
    fuse.inputB = box2Idx;
    const int fuseIdx = doc.addFeature(fuse);

    const double before = volumeOf(doc.shapeAt(fuseIdx));

    // Grow the first box; the fused result must reflect it without
    // re-adding anything.
    Feature3D biggerBox = box;
    biggerBox.p1 = biggerBox.p2 = biggerBox.p3 = 20;
    doc.updateFeature(boxIdx, biggerBox);

    REQUIRE(doc.isValid(fuseIdx));
    const double after = volumeOf(doc.shapeAt(fuseIdx));
    REQUIRE(after > before);
}

TEST_CASE("removeFeature refuses to delete a feature something else still depends on", "[core3d][feature]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    const int boxIdx = doc.addFeature(box);
    Feature3D box2 = box;
    const int box2Idx = doc.addFeature(box2);
    Feature3D fuse;
    fuse.type = FeatureType::Union;
    fuse.inputA = boxIdx;
    fuse.inputB = box2Idx;
    doc.addFeature(fuse);

    REQUIRE_FALSE(doc.removeFeature(boxIdx));
    REQUIRE(doc.features().size() == 3);
}

TEST_CASE("Document3D's CommandStack undoes and redoes adding a feature", "[core3d][undo]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 10;

    auto cmd = std::make_unique<AddFeature3DCommand>(doc, box);
    AddFeature3DCommand* cmdPtr = cmd.get();
    doc.commandStack().execute(std::move(cmd));
    REQUIRE(doc.features().size() == 1);
    REQUIRE(doc.isValid(cmdPtr->index()));

    doc.commandStack().undo();
    REQUIRE(doc.features().empty());

    doc.commandStack().redo();
    REQUIRE(doc.features().size() == 1);
    REQUIRE(doc.isValid(0));
}

TEST_CASE("Document3D's CommandStack undoes an edit back to the exact prior parameters", "[core3d][undo]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 10;
    auto addCmd = std::make_unique<AddFeature3DCommand>(doc, box);
    doc.commandStack().execute(std::move(addCmd));

    const double before = volumeOf(doc.shapeAt(0));

    Feature3D bigger = box;
    bigger.p1 = bigger.p2 = bigger.p3 = 20;
    doc.commandStack().execute(std::make_unique<UpdateFeature3DCommand>(doc, 0, bigger));
    REQUIRE(volumeOf(doc.shapeAt(0)) > before);

    doc.commandStack().undo();
    REQUIRE(volumeOf(doc.shapeAt(0)) == Approx(before).margin(1e-6));
}

TEST_CASE("Document3D's CommandStack restores a removed feature to its exact original position",
         "[core3d][undo]") {
    Document3D doc;
    Feature3D a;
    a.type = FeatureType::Box;
    a.p1 = a.p2 = a.p3 = 5;
    Feature3D b;
    b.type = FeatureType::Sphere;
    b.p1 = 3;
    doc.commandStack().execute(std::make_unique<AddFeature3DCommand>(doc, a));
    doc.commandStack().execute(std::make_unique<AddFeature3DCommand>(doc, b));

    doc.commandStack().execute(std::make_unique<RemoveFeature3DCommand>(doc, 0));
    REQUIRE(doc.features().size() == 1);
    REQUIRE(doc.features()[0].type == FeatureType::Sphere);

    doc.commandStack().undo();
    REQUIRE(doc.features().size() == 2);
    REQUIRE(doc.features()[0].type == FeatureType::Box);
    REQUIRE(doc.features()[1].type == FeatureType::Sphere);
    REQUIRE(doc.isValid(0));
    REQUIRE(doc.isValid(1));
}

TEST_CASE("Document3D stores finished sketches for a later sketch-based feature to consume",
         "[core3d][sketch]") {
    Document3D doc;
    REQUIRE(doc.sketches().empty());

    Sketch sketch;
    sketch.addPoint(Point2D(0, 0), true);
    sketch.addPoint(Point2D(10, 0));
    sketch.addLine(0, 1);

    const int index = doc.addSketch(sketch);
    REQUIRE(index == 0);
    REQUIRE(doc.sketches().size() == 1);
    REQUIRE(doc.sketches()[0].lines().size() == 1);
}

namespace {
void boundingExtents(const TopoDS_Shape& shape, double& dx, double& dy, double& dz) {
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    dx = xmax - xmin;
    dy = ymax - ymin;
    dz = zmax - zmin;
}
} // namespace

TEST_CASE("A rotated Box keeps its volume but swaps its X/Y bounding extents", "[core3d][rotation]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = 20.0; // dx
    box.p2 = 5.0;  // dy
    box.p3 = 8.0;  // dz
    box.rotAxisX = 0;
    box.rotAxisY = 0;
    box.rotAxisZ = 1;
    box.rotAngle = 90.0;
    const int idx = doc.addFeature(box);

    REQUIRE(doc.isValid(idx));

    GProp_GProps props;
    BRepGProp::VolumeProperties(doc.shapeAt(idx), props);
    REQUIRE(props.Mass() == Approx(20.0 * 5.0 * 8.0).margin(1e-6));

    double dx = 0, dy = 0, dz = 0;
    boundingExtents(doc.shapeAt(idx), dx, dy, dz);
    // A 90-degree spin around Z swaps which world axis the box's dx/dy
    // extents land on.
    REQUIRE(dx == Approx(5.0).margin(1e-6));
    REQUIRE(dy == Approx(20.0).margin(1e-6));
    REQUIRE(dz == Approx(8.0).margin(1e-6));
}

TEST_CASE("An unrotated Box (rotAngle == 0) is unaffected by rotation fields", "[core3d][rotation]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = 20.0;
    box.p2 = 5.0;
    box.p3 = 8.0;
    const int idx = doc.addFeature(box); // rotAngle defaults to 0.0

    REQUIRE(doc.isValid(idx));
    double dx = 0, dy = 0, dz = 0;
    boundingExtents(doc.shapeAt(idx), dx, dy, dz);
    REQUIRE(dx == Approx(20.0).margin(1e-6));
    REQUIRE(dy == Approx(5.0).margin(1e-6));
    REQUIRE(dz == Approx(8.0).margin(1e-6));
}

TEST_CASE("An Imported feature is translated to its posX/Y/Z", "[core3d][rotation][import]") {
    Document3D doc;
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const int importIdx = doc.addImportedShape(box);

    Feature3D imported;
    imported.type = FeatureType::Imported;
    imported.importIndex = importIdx;
    imported.posX = 100.0;
    imported.posY = 50.0;
    imported.posZ = 0.0;
    const int idx = doc.addFeature(imported);

    REQUIRE(doc.isValid(idx));
    double dx = 0, dy = 0, dz = 0;
    boundingExtents(doc.shapeAt(idx), dx, dy, dz);
    REQUIRE(dx == Approx(10.0).margin(1e-6)); // size unaffected by translation

    Bnd_Box bounds;
    BRepBndLib::Add(doc.shapeAt(idx), bounds);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    REQUIRE(xmin == Approx(100.0).margin(1e-6));
    REQUIRE(ymin == Approx(50.0).margin(1e-6));
}

TEST_CASE("Document3D Helix builds a coil with the exact pipe-volume identity", "[core3d][helix]") {
    // A pipe/sweep of constant cross-section along ANY curve has volume =
    // (cross-section area) * (path arc length) exactly, regardless of the
    // path's shape -- the same volume-conservation identity SheetMetal's
    // own bend validation already relies on. A helix's own arc length is
    // exactly turns * sqrt((2*pi*radius)^2 + pitch^2).
    Document3D doc;
    Feature3D helix;
    helix.type = FeatureType::Helix;
    helix.p1 = 10.0; // helix radius
    helix.p2 = 2.0;  // pitch
    helix.p3 = 20.0; // height -> 10 turns
    helix.p4 = 0.5;  // profile (wire) radius
    const int helixIdx = doc.addFeature(helix);

    REQUIRE(doc.isValid(helixIdx));

    const double turns = helix.p3 / helix.p2;
    const double archLength = turns * std::sqrt(std::pow(2.0 * M_PI * helix.p1, 2) + helix.p2 * helix.p2);
    const double crossSectionArea = M_PI * helix.p4 * helix.p4;
    const double expectedVolume = crossSectionArea * archLength;

    REQUIRE(volumeOf(doc.shapeAt(helixIdx)) == Approx(expectedVolume).epsilon(0.01));
}

TEST_CASE("Document3D Helix spans the requested height along its axis", "[core3d][helix]") {
    Document3D doc;
    Feature3D helix;
    helix.type = FeatureType::Helix;
    helix.p1 = 5.0;
    helix.p2 = 1.0;
    helix.p3 = 10.0;
    helix.p4 = 0.3;
    helix.posX = helix.posY = helix.posZ = 0.0;
    helix.dirX = 0.0;
    helix.dirY = 0.0;
    helix.dirZ = 1.0;
    const int helixIdx = doc.addFeature(helix);
    REQUIRE(doc.isValid(helixIdx));

    Bnd_Box bounds;
    BRepBndLib::Add(doc.shapeAt(helixIdx), bounds);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    // The coil covers its full nominal height, plus a bit of padding from
    // the wire's own thickness at each end (exactly how much depends on
    // the end caps' tilt relative to the axis -- a tighter closed-form
    // margin isn't attempted here, the exact pipe-volume identity in the
    // test above is the real correctness proof; this just sanity-checks
    // axial coverage).
    REQUIRE(zmin < 1e-6);
    REQUIRE(zmax > helix.p3 - 1e-6);
    // A generous bound, not a tight one -- end-cap tilt can add several
    // times the wire radius of padding for a shallow-pitch coil like this
    // one (pitch=1 over a radius=5 circumference of ~31.4, so the tangent
    // -- and thus the end caps -- sit closer to vertical than horizontal).
    // Just guards against something having gone wildly wrong (e.g. a
    // second, unwanted full turn's worth of extra height).
    REQUIRE((zmax - zmin) < helix.p3 * 1.5);
}

TEST_CASE("Document3D Helix rejects degenerate parameters", "[core3d][helix]") {
    Document3D doc;
    Feature3D base;
    base.type = FeatureType::Helix;
    base.p1 = 10.0;
    base.p2 = 2.0;
    base.p3 = 20.0;
    base.p4 = 1.0;

    auto tryBuild = [&](Feature3D f) { return doc.isValid(doc.addFeature(f)); };

    Feature3D noRadius = base;
    noRadius.p1 = 0.0;
    REQUIRE_FALSE(tryBuild(noRadius));

    Feature3D noPitch = base;
    noPitch.p2 = 0.0;
    REQUIRE_FALSE(tryBuild(noPitch));

    Feature3D noHeight = base;
    noHeight.p3 = 0.0;
    REQUIRE_FALSE(tryBuild(noHeight));

    Feature3D noProfile = base;
    noProfile.p4 = 0.0;
    REQUIRE_FALSE(tryBuild(noProfile));

    Feature3D noDir = base;
    noDir.dirX = noDir.dirY = noDir.dirZ = 0.0;
    REQUIRE_FALSE(tryBuild(noDir));
}

TEST_CASE("Document3D Hole drills a simple through-diameter hole with the exact cylinder volume", "[core3d][hole]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);
    const double boxVolume = 20.0 * 20.0 * 20.0;

    Feature3D hole;
    hole.type = FeatureType::Hole;
    hole.inputA = boxIdx;
    hole.p1 = 4.0; // diameter
    hole.p2 = 20.0; // full depth (through)
    hole.posX = hole.posY = 10.0; // centered
    hole.posZ = 0.0;
    hole.dirX = 0.0;
    hole.dirY = 0.0;
    hole.dirZ = 1.0;
    hole.count = 0; // Simple
    const int holeIdx = doc.addFeature(hole);

    REQUIRE(doc.isValid(holeIdx));
    const double drilledVolume = M_PI * (hole.p1 / 2.0) * (hole.p1 / 2.0) * hole.p2;
    REQUIRE(volumeOf(doc.shapeAt(holeIdx)) == Approx(boxVolume - drilledVolume).margin(1e-3));
}

TEST_CASE("Document3D Hole counterbore matches the exact two-diameter-strip volume", "[core3d][hole]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);
    const double boxVolume = 20.0 * 20.0 * 20.0;

    Feature3D hole;
    hole.type = FeatureType::Hole;
    hole.inputA = boxIdx;
    hole.p1 = 4.0;  // through diameter
    hole.p2 = 20.0; // through depth
    hole.p3 = 8.0;  // counterbore diameter
    hole.p4 = 3.0;  // counterbore depth
    hole.posX = hole.posY = 10.0;
    hole.posZ = 0.0;
    hole.dirX = 0.0;
    hole.dirY = 0.0;
    hole.dirZ = 1.0;
    hole.count = 1; // Counterbore
    const int holeIdx = doc.addFeature(hole);

    REQUIRE(doc.isValid(holeIdx));
    // Exact tool volume: the WIDE counterbore radius dominates strip
    // [0,p4] (the narrow hole is a proper subset there), the narrow
    // radius alone covers the rest [p4,p2].
    const double wideR = hole.p3 / 2.0, narrowR = hole.p1 / 2.0;
    const double toolVolume = M_PI * wideR * wideR * hole.p4 + M_PI * narrowR * narrowR * (hole.p2 - hole.p4);
    REQUIRE(volumeOf(doc.shapeAt(holeIdx)) == Approx(boxVolume - toolVolume).margin(1e-2));
}

TEST_CASE("Document3D Hole countersink matches the exact frustum-plus-cylinder volume", "[core3d][hole]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);
    const double boxVolume = 20.0 * 20.0 * 20.0;

    Feature3D hole;
    hole.type = FeatureType::Hole;
    hole.inputA = boxIdx;
    hole.p1 = 4.0;  // through diameter
    hole.p2 = 20.0; // through depth
    hole.p3 = 8.0;  // countersink diameter
    hole.p4 = 90.0; // full included angle, degrees
    hole.posX = hole.posY = 10.0;
    hole.posZ = 0.0;
    hole.dirX = 0.0;
    hole.dirY = 0.0;
    hole.dirZ = 1.0;
    hole.count = 2; // Countersink
    const int holeIdx = doc.addFeature(hole);

    REQUIRE(doc.isValid(holeIdx));

    const double wideR = hole.p3 / 2.0, narrowR = hole.p1 / 2.0;
    const double countersinkDepth = (wideR - narrowR) / std::tan(hole.p4 * M_PI / 360.0);
    // Frustum volume (standard formula) + the narrow cylinder's remaining
    // length below the frustum.
    const double frustumVolume =
        (M_PI * countersinkDepth / 3.0) * (wideR * wideR + wideR * narrowR + narrowR * narrowR);
    const double toolVolume = frustumVolume + M_PI * narrowR * narrowR * (hole.p2 - countersinkDepth);
    REQUIRE(volumeOf(doc.shapeAt(holeIdx)) == Approx(boxVolume - toolVolume).margin(1e-2));
}

TEST_CASE("Document3D Hole rejects degenerate parameters", "[core3d][hole]") {
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    Feature3D base;
    base.type = FeatureType::Hole;
    base.inputA = boxIdx;
    base.p1 = 4.0;
    base.p2 = 20.0;
    base.dirZ = 1.0;

    auto tryBuild = [&](Feature3D f) { return doc.isValid(doc.addFeature(f)); };

    Feature3D noTarget = base;
    noTarget.inputA = -1;
    REQUIRE_FALSE(tryBuild(noTarget));

    Feature3D noDiameter = base;
    noDiameter.p1 = 0.0;
    REQUIRE_FALSE(tryBuild(noDiameter));

    Feature3D noDepth = base;
    noDepth.p2 = 0.0;
    REQUIRE_FALSE(tryBuild(noDepth));

    Feature3D noDir = base;
    noDir.dirX = noDir.dirY = noDir.dirZ = 0.0;
    REQUIRE_FALSE(tryBuild(noDir));
}
