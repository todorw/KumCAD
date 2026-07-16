#include "core/core3d/Commands3D.h"
#include "core/core3d/Document3D.h"

#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
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
