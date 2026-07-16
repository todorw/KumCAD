#include "core/core3d/Document3D.h"

#include <BRepGProp.hxx>
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
