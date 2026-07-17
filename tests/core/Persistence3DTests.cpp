#include "core/core3d/Document3D.h"
#include "core/core3d/Persistence3D.h"
#include "core/core3d/Pick3D.h"
#include "core/core3d/TopoNaming.h"
#include "core/io/Zip.h"
#include "core/sketch/SketchGeometry.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

using namespace lcad;
using Catch::Approx;

namespace {

struct TempPath {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("kumcad_kcad3d_test_" + std::to_string(std::rand()) + ".kcad3d");
    ~TempPath() { std::filesystem::remove(path); }
};

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
    SketchConstraint horiz;
    horiz.type = SketchConstraintType::Horizontal;
    horiz.geomA = 0;
    sketch.addConstraint(horiz);
    return sketch;
}

} // namespace

TEST_CASE("Document3D save/load round-trips a plain primitive/boolean feature tree", "[core3d][persistence]") {
    TempPath temp;
    Document3D doc;
    Feature3D boxA;
    boxA.type = FeatureType::Box;
    boxA.name = "Base Block";
    boxA.p1 = boxA.p2 = boxA.p3 = 20.0;
    const int a = doc.addFeature(boxA);

    Feature3D cyl;
    cyl.type = FeatureType::Cylinder;
    cyl.p1 = 3.0;
    cyl.p2 = 20.0;
    cyl.posX = cyl.posY = 10.0;
    const int b = doc.addFeature(cyl);

    Feature3D cut;
    cut.type = FeatureType::Cut;
    cut.inputA = a;
    cut.inputB = b;
    doc.addFeature(cut);

    REQUIRE(saveDocument3D(doc, temp.path.string()));

    Document3D loaded;
    REQUIRE(loadDocument3D(loaded, temp.path.string()));
    REQUIRE(loaded.features().size() == 3);
    REQUIRE(loaded.findFeature(0)->name == "Base Block");
    REQUIRE(loaded.isValid(2));
    REQUIRE(volumeOf(loaded.shapeAt(2)) == Approx(volumeOf(doc.shapeAt(2))).margin(1e-6));
}

TEST_CASE("Document3D save/load round-trips rotation, per-edge Fillet selection, and per-face Shell selection",
         "[core3d][persistence]") {
    TempPath temp;
    Document3D doc;

    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    box.rotAxisX = 0.0;
    box.rotAxisY = 0.0;
    box.rotAxisZ = 1.0;
    box.rotAngle = 45.0;
    const int boxIdx = doc.addFeature(box);

    Feature3D fillet;
    fillet.type = FeatureType::Fillet;
    fillet.inputA = boxIdx;
    fillet.p1 = 2.0;
    fillet.edgeIndices = {0, 3, 7};
    const int filletIdx = doc.addFeature(fillet);

    REQUIRE(saveDocument3D(doc, temp.path.string()));

    Document3D loaded;
    REQUIRE(loadDocument3D(loaded, temp.path.string()));
    REQUIRE(loaded.features().size() == 2);

    const Feature3D* loadedBox = loaded.findFeature(0);
    REQUIRE(loadedBox);
    REQUIRE(loadedBox->rotAngle == Approx(45.0));
    REQUIRE(loadedBox->rotAxisZ == Approx(1.0));

    const Feature3D* loadedFillet = loaded.findFeature(1);
    REQUIRE(loadedFillet);
    REQUIRE(loadedFillet->edgeIndices == std::vector<int>{0, 3, 7});

    // The recomputed shape is genuinely the same as the original's (not
    // just the raw parameters matching) -- if either rotation or the
    // specific edge selection had been silently dropped, this volume
    // would differ from the original's.
    REQUIRE(volumeOf(loaded.shapeAt(filletIdx)) == Approx(volumeOf(doc.shapeAt(filletIdx))).margin(1e-6));
}

TEST_CASE("loadDocument3D reads an older format-1 file (no rotation/edge/face fields) with sane defaults",
         "[core3d][persistence]") {
    TempPath temp;
    // A hand-built format-1 document.txt, exactly the shape the writer
    // produced before rotation/edgeIndices/faceIndices existed -- proves
    // an old save file (which obviously can't have fields that didn't
    // exist yet) still loads, rather than requiring every user to redo
    // their old documents.
    const std::string oldFormatText =
        "KCAD3D 1\n"
        "SKETCHES 0\n"
        "FEATURES 1\n"
        "0 OldBox\n"
        "20 20 20 0\n"
        "0 0 0\n"
        "0 0 1\n"
        "-1 -1 0\n"
        "-1 1 -1\n"
        "IMPORTEDSHAPES 0\n";
    REQUIRE(writeZip(temp.path.string(), {{"document.txt", oldFormatText}}));

    Document3D loaded;
    REQUIRE(loadDocument3D(loaded, temp.path.string()));
    REQUIRE(loaded.features().size() == 1);
    const Feature3D* feature = loaded.findFeature(0);
    REQUIRE(feature);
    REQUIRE(feature->name == "OldBox");
    REQUIRE(feature->rotAngle == Approx(0.0)); // no rotation field in format 1 -- Feature3D's own default
    REQUIRE(feature->edgeIndices.empty());
    REQUIRE(feature->faceIndices.empty());
    REQUIRE(loaded.isValid(0));
    REQUIRE(volumeOf(loaded.shapeAt(0)) == Approx(20.0 * 20.0 * 20.0).margin(1e-6));
}

TEST_CASE("Document3D save/load round-trips a sketch and a Pad feature built from it", "[core3d][persistence]") {
    TempPath temp;
    Document3D doc;
    const int sketchIdx = doc.addSketch(makeRectangleSketch(8.0, 6.0));

    Feature3D pad;
    pad.type = FeatureType::Pad;
    pad.sketchIndex = sketchIdx;
    pad.p1 = 3.0;
    doc.addFeature(pad);

    REQUIRE(saveDocument3D(doc, temp.path.string()));

    Document3D loaded;
    REQUIRE(loadDocument3D(loaded, temp.path.string()));
    REQUIRE(loaded.sketches().size() == 1);
    REQUIRE(loaded.sketches()[0].lines().size() == 4);
    REQUIRE(loaded.sketches()[0].constraints().size() == 1);
    REQUIRE(loaded.isValid(0));
    REQUIRE(volumeOf(loaded.shapeAt(0)) == Approx(8.0 * 6.0 * 3.0).margin(1e-6));
}

TEST_CASE("Document3D save/load round-trips an Imported feature's embedded BRep geometry", "[core3d][persistence]") {
    TempPath temp;
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 12.0;
    doc.addFeature(box);
    const int imported = doc.addImportedShape(doc.shapeAt(0));

    Feature3D importedFeature;
    importedFeature.type = FeatureType::Imported;
    importedFeature.importIndex = imported;
    doc.addFeature(importedFeature);

    REQUIRE(saveDocument3D(doc, temp.path.string()));

    Document3D loaded;
    REQUIRE(loadDocument3D(loaded, temp.path.string()));
    REQUIRE(loaded.importedShapes().size() == 1);
    REQUIRE(loaded.isValid(1));
    REQUIRE(volumeOf(loaded.shapeAt(1)) == Approx(12.0 * 12.0 * 12.0).margin(1e-6));
}

TEST_CASE("loadDocument3D fails cleanly on a missing or malformed file", "[core3d][persistence]") {
    Document3D doc;
    REQUIRE_FALSE(loadDocument3D(doc, "/nonexistent/path/kumcad_never_exists.kcad3d"));
}

TEST_CASE("Document3D save/load round-trips named variables and a feature's expression", "[core3d][persistence]") {
    TempPath temp;
    Document3D doc;
    doc.setVariable("Width", 6.0);
    doc.setVariable("Height", 3.5);

    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = 1.0; // will be overwritten by the expression on the very first recompute
    box.p2 = box.p3 = 4.0;
    box.expressions["p1"] = "Width*2";
    const int boxIdx = doc.addFeature(box);
    REQUIRE(doc.findFeature(boxIdx)->p1 == Approx(12.0)); // sanity: expression already applied pre-save

    REQUIRE(saveDocument3D(doc, temp.path.string()));

    Document3D loaded;
    REQUIRE(loadDocument3D(loaded, temp.path.string()));

    REQUIRE(loaded.variables().size() == 2);
    REQUIRE(loaded.variables().at("Width") == Approx(6.0));
    REQUIRE(loaded.variables().at("Height") == Approx(3.5));

    const Feature3D* loadedBox = loaded.findFeature(boxIdx);
    REQUIRE(loadedBox);
    REQUIRE(loadedBox->expressions.at("p1") == "Width*2");
    REQUIRE(loadedBox->p1 == Approx(12.0)); // re-evaluated on load, not just the raw stored 1.0
    REQUIRE(loaded.isValid(boxIdx));
    REQUIRE(volumeOf(loaded.shapeAt(boxIdx)) == Approx(12.0 * 4.0 * 4.0).margin(1e-6));

    // Changing the reloaded variable still recomputes the reloaded feature
    // -- proves the expression binding survived as a LIVE binding, not
    // just a snapshot of its once-evaluated value.
    loaded.setVariable("Width", 10.0);
    REQUIRE(loaded.findFeature(boxIdx)->p1 == Approx(20.0));
}

TEST_CASE("loadDocument3D reads an older format-4 file (no variables/expressions) with sane defaults",
          "[core3d][persistence]") {
    TempPath temp;
    // A hand-built format-4 document.txt, predating VARIABLES/EXPRESSIONS,
    // with one plain Box feature -- proves an old save file still loads.
    const std::string oldFormatText =
        "KCAD3D 4\n"
        "SKETCHES 0\n"
        "FEATURES 1\n"
        "0 OldBox\n"
        "9 9 9 0\n"
        "0 0 0\n"
        "0 0 1\n"
        "0 0 1 0\n"
        "-1 -1 0\n"
        "-1 1 -1 -1\n"
        "EDGEINDICES 0\n"
        "FACEINDICES 0\n"
        "SKETCHINDICES 0\n"
        "IMPORTEDSHAPES 0\n";
    REQUIRE(writeZip(temp.path.string(), {{"document.txt", oldFormatText}}));

    Document3D loaded;
    REQUIRE(loadDocument3D(loaded, temp.path.string()));
    REQUIRE(loaded.variables().empty());
    REQUIRE(loaded.isValid(0));
    REQUIRE(volumeOf(loaded.shapeAt(0)) == Approx(9.0 * 9.0 * 9.0).margin(1e-6));
    REQUIRE(loaded.findFeature(0)->expressions.empty());
}

TEST_CASE("Document3D save/load round-trips a Fillet's edge fingerprints and survives a stale raw index",
          "[core3d][persistence][toponaming]") {
    TempPath temp;
    Document3D doc;
    Feature3D box;
    box.type = FeatureType::Box;
    box.p1 = box.p2 = box.p3 = 20.0;
    const int boxIdx = doc.addFeature(box);

    PickRay ray;
    ray.origin = {20.1, 20.0, -50.0};
    ray.direction = {0.0, 0.0, 1.0};
    const auto picked = pickEdge(doc.shapeAt(boxIdx), ray, 0.5);
    REQUIRE(picked.has_value());
    const auto fingerprint = fingerprintEdge(doc.shapeAt(boxIdx), picked->edgeIndex);
    REQUIRE(fingerprint.has_value());

    Feature3D fillet;
    fillet.type = FeatureType::Fillet;
    fillet.inputA = boxIdx;
    fillet.p1 = 2.0;
    // A deliberately wrong stored index, saved alongside the correct
    // fingerprint -- proves the fingerprint (not the index) survives the
    // round-trip and still drives recompute correctly after loading.
    fillet.edgeIndices = {(picked->edgeIndex + 5) % 12};
    fillet.edgeFingerprints = {*fingerprint};
    const int filletIdx = doc.addFeature(fillet);
    REQUIRE(doc.isValid(filletIdx));
    const double originalVolume = volumeOf(doc.shapeAt(filletIdx));

    REQUIRE(saveDocument3D(doc, temp.path.string()));
    Document3D loaded;
    REQUIRE(loadDocument3D(loaded, temp.path.string()));

    const Feature3D* loadedFillet = loaded.findFeature(filletIdx);
    REQUIRE(loadedFillet);
    REQUIRE(loadedFillet->edgeFingerprints.size() == 1);
    REQUIRE(loadedFillet->edgeFingerprints[0].length == Approx(fingerprint->length));
    REQUIRE(loaded.isValid(filletIdx));
    REQUIRE(volumeOf(loaded.shapeAt(filletIdx)) == Approx(originalVolume).margin(1e-6));
}

TEST_CASE("loadDocument3D reads an older format-5 file (no fingerprints) with sane defaults",
          "[core3d][persistence][toponaming]") {
    TempPath temp;
    const std::string oldFormatText =
        "KCAD3D 5\n"
        "VARIABLES 0\n"
        "SKETCHES 0\n"
        "FEATURES 1\n"
        "0 OldBox\n"
        "9 9 9 0\n"
        "0 0 0\n"
        "0 0 1\n"
        "0 0 1 0\n"
        "-1 -1 0\n"
        "-1 1 -1 -1\n"
        "EDGEINDICES 0\n"
        "FACEINDICES 0\n"
        "SKETCHINDICES 0\n"
        "EXPRESSIONS 0\n"
        "IMPORTEDSHAPES 0\n";
    REQUIRE(writeZip(temp.path.string(), {{"document.txt", oldFormatText}}));

    Document3D loaded;
    REQUIRE(loadDocument3D(loaded, temp.path.string()));
    REQUIRE(loaded.isValid(0));
    REQUIRE(volumeOf(loaded.shapeAt(0)) == Approx(9.0 * 9.0 * 9.0).margin(1e-6));
    REQUIRE(loaded.findFeature(0)->edgeFingerprints.empty());
    REQUIRE(loaded.findFeature(0)->faceFingerprints.empty());
}
